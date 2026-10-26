/*
 * Bober AtomS3R LCD driver v1.11.1; v1.7 transport preserved.
 * Pins: MOSI=21 SCLK=15 CS=14 D/C=42 RST=48; LP5562 I2C SDA=45/SCL=0.
 *
 * Unlike v1.6, CS is GPIO-controlled and held LOW across each complete
 * LCD command + its parameters, and across RAMWR + the entire framebuffer.
 * This avoids breaking a controller command on a hardware-CS transaction
 * boundary. SPI3_HOST matches the AtomS3R configuration used by M5GFX.
 *
 * Two panel populations exist; M5Stack documents GC9107 -> ST7735 change
 * May 2026. Prefer ST7735S for newer hardware; older boards change to 0.
 * Software quarter-turn rotation from the onboard BMI270 accelerometer.
 */
#include "bober.h"
#include "display_idle.h"
#include "animation.h"
#include "imu.h"
#include "orientation.h"
#include "freertos/task.h"
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

static const char *TAG = "screen";
#define BOBER_LCD_ST7735S 1
#define LCD_HOST SPI3_HOST
#define LCD_CS GPIO_NUM_14
#define LCD_DC GPIO_NUM_42
#define LCD_RST GPIO_NUM_48
#define LCD_CLOCK_HZ (10 * 1000 * 1000)
#define LCD_ROW_BYTES (128 * 2)
#ifndef BOBER_LCD_BGR
#define BOBER_LCD_BGR 1
#endif
#define LCD_MADCTL_COLOR (BOBER_LCD_BGR ? 0x08 : 0x00)
#ifndef BOBER_LCD_ANIMATION
#define BOBER_LCD_ANIMATION 1
#endif

static spi_device_handle_t lcd;
static i2c_master_dev_handle_t backlight;
static i2c_master_bus_handle_t internal_bus;
static uint8_t *pixels;
static uint32_t lcd_bytes;
static uint32_t lcd_tx_errors;
static void delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static inline void lcd_select(void) { gpio_set_level(LCD_CS, 0); }
static inline void lcd_deselect(void) { gpio_set_level(LCD_CS, 1); }

/* Caller owns /CS. Does not change /CS between a command and its parameters. */
static esp_err_t lcd_tx(bool is_command, const uint8_t *data, size_t size)
{
    if (!lcd || !data || size == 0) return ESP_ERR_INVALID_ARG;
    gpio_set_level(LCD_DC, is_command ? 0 : 1);
    spi_transaction_t t = {0};
    t.length = size * 8;
    if (size <= sizeof(t.tx_data)) {
        t.flags = SPI_TRANS_USE_TXDATA;
        memcpy(t.tx_data, data, size);
    } else {
        t.tx_buffer = data;
    }
    esp_err_t e = spi_device_polling_transmit(lcd, &t);
    if (e == ESP_OK) {
        lcd_bytes += (uint32_t)size;
    } else {
        ++lcd_tx_errors;
        ESP_LOGE(TAG, "LCD SPI %s %u bytes failed: %s", is_command ? "CMD" : "DATA",
                 (unsigned)size, esp_err_to_name(e));
    }
    return e;
}

static bool lcd_reg(uint8_t command, const uint8_t *data, size_t len)
{
    lcd_select();
    bool ok = lcd_tx(true, &command, 1) == ESP_OK;
    if (len) ok = (lcd_tx(false, data, len) == ESP_OK) && ok;
    lcd_deselect();
    return ok;
}
static bool lcd_cmd(uint8_t cmd) { return lcd_reg(cmd, NULL, 0); }
static bool lcd_reg1(uint8_t cmd, uint8_t val) { return lcd_reg(cmd, &val, 1); }

static bool bl_write(uint8_t r, uint8_t v)
{
    uint8_t d[2] = {r, v};
    esp_err_t e = i2c_master_transmit(backlight, d, sizeof(d), 100);
    if (e != ESP_OK) ESP_LOGE(TAG, "LP5562 reg=0x%02x: %s", r, esp_err_to_name(e));
    return e == ESP_OK;
}
static bool init_backlight(void)
{
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t b = {
        .i2c_port = I2C_NUM_1, .sda_io_num = GPIO_NUM_45,
        .scl_io_num = GPIO_NUM_0, .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true
    };
    esp_err_t e = i2c_new_master_bus(&b, &bus);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "LP5562 bus: %s", esp_err_to_name(e));
        return false;
    }
    internal_bus=bus;
    i2c_device_config_t d = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x30,
        .scl_speed_hz = 400000
    };
    e = i2c_master_bus_add_device(bus, &d, &backlight);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "LP5562 attach: %s", esp_err_to_name(e));
        return false;
    }
    ESP_LOGI(TAG, "LP5562 probe: %s", esp_err_to_name(i2c_master_probe(bus, 0x30, 200)));
    bool ok = bl_write(0x00, 0x40);
    delay_ms(2);
    ok = bl_write(0x08, 0x01) && ok;
    ok = bl_write(0x70, 0x00) && ok;
    ok = bl_write(0x0e, 0xff) && ok;
    ESP_LOGI(TAG, "LP5562 brightness=255 %s", ok ? "OK" : "FAILED");
    return ok;
}

/*
 * These commands are from LovyanGFX Panel_ST7735S::getInitCommands()
 * Rcmd1 and Rcmd2, not the shortened v1.6 sequence.
 */
static bool st7735_init(void)
{
    bool ok = lcd_cmd(0x01); delay_ms(150); // SWRESET
    ok = lcd_cmd(0x11) && ok; delay_ms(500); // SLPOUT
    const uint8_t frm3[] = {0x01, 0x2c, 0x2d};
    const uint8_t frm6[] = {0x01, 0x2c, 0x2d, 0x01, 0x2c, 0x2d};
    const uint8_t pwr1[] = {0xa2, 0x02, 0x84};
    const uint8_t pwr3[] = {0x0a, 0x00};
    const uint8_t pwr4[] = {0x8a, 0x2a};
    const uint8_t pwr5[] = {0x8a, 0xee};
    const uint8_t gamma_p[] = {
        0x02,0x1c,0x07,0x12,0x37,0x32,0x29,0x2d,
        0x29,0x25,0x2b,0x39,0x00,0x01,0x03,0x10
    };
    const uint8_t gamma_n[] = {
        0x03,0x1d,0x07,0x06,0x2e,0x2c,0x29,0x2d,
        0x2e,0x2e,0x37,0x3f,0x00,0x00,0x02,0x10
    };
    ok = lcd_reg(0xb1, frm3, sizeof(frm3)) && ok;
    ok = lcd_reg(0xb2, frm3, sizeof(frm3)) && ok;
    ok = lcd_reg(0xb3, frm6, sizeof(frm6)) && ok;
    ok = lcd_reg1(0xb4, 0x07) && ok;
    ok = lcd_reg(0xc0, pwr1, sizeof(pwr1)) && ok;
    ok = lcd_reg1(0xc1, 0xc5) && ok;
    ok = lcd_reg(0xc2, pwr3, sizeof(pwr3)) && ok;
    ok = lcd_reg(0xc3, pwr4, sizeof(pwr4)) && ok;
    ok = lcd_reg(0xc4, pwr5, sizeof(pwr5)) && ok;
    ok = lcd_reg1(0xc5, 0x0e) && ok;
    ok = lcd_reg(0xe0, gamma_p, sizeof(gamma_p)) && ok;
    ok = lcd_reg(0xe1, gamma_n, sizeof(gamma_n)) && ok;
    ok = lcd_reg1(0x3a, 0x05) && ok; // RGB565
    ok = lcd_reg1(0x36, LCD_MADCTL_COLOR) && ok; // MADCTL: BGR color order; no X/Y swapping
    ok = lcd_cmd(0x13) && ok; delay_ms(10); // NORON
    ok = lcd_cmd(0x21) && ok; // invert=true in M5GFX's AtomS3R ST7735S profile
    ok = lcd_cmd(0x29) && ok; delay_ms(100); // DISPON
    return ok;
}

/* Byte-for-byte controller-specific register values from LovyanGFX Panel_GC9107. */
static bool gc9107_init(void)
{
    bool ok = lcd_cmd(0xfe); delay_ms(5);
    ok = lcd_cmd(0xef) && ok; delay_ms(5);
    static const uint8_t pair[][2] = {
        {0xb0,0xc0},{0xb2,0x2f},{0xb3,0x03},{0xb6,0x19},
        {0xb7,0x01},{0xac,0xcb},{0xab,0x0e},{0xb4,0x04},
        {0xa8,0x19},{0xb8,0x08},{0xe8,0x24},{0xe9,0x48},
        {0xea,0x22},{0xc6,0x30},{0xc7,0x18}
    };
    for (size_t i = 0; i < sizeof(pair)/sizeof(pair[0]); ++i)
        ok = lcd_reg1(pair[i][0], pair[i][1]) && ok;
    const uint8_t f0[14] = {0x01,0x2b,0x23,0x3c,0xb7,0x12,0x17,0x60,0x00,0x06,0x0c,0x17,0x12,0x1f};
    const uint8_t f1[14] = {0x05,0x2e,0x2d,0x44,0xd6,0x15,0x17,0xa0,0x02,0x0d,0x0d,0x1a,0x18,0x1f};
    ok = lcd_reg(0xf0, f0, sizeof(f0)) && ok;
    ok = lcd_reg(0xf1, f1, sizeof(f1)) && ok;
    ok = lcd_cmd(0x11) && ok; delay_ms(120);
    ok = lcd_reg1(0x3a, 0x05) && ok; // RGB565
    ok = lcd_reg1(0x36, LCD_MADCTL_COLOR) && ok;
    ok = lcd_cmd(0x29) && ok; delay_ms(100);
    return ok;
}

static bool lcd_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LCD_CS) | (1ULL << LCD_DC) | (1ULL << LCD_RST),
        .mode = GPIO_MODE_OUTPUT
    };
    esp_err_t e = gpio_config(&io);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "LCD GPIO: %s", esp_err_to_name(e)); return false;
    }
    lcd_deselect(); // IMPORTANT: keep display unselected until SPI is ready.
    gpio_set_level(LCD_DC, 1);
    gpio_set_level(LCD_RST, 1); delay_ms(15);
    gpio_set_level(LCD_RST, 0); delay_ms(40);
    gpio_set_level(LCD_RST, 1); delay_ms(120);
    spi_bus_config_t bus = {
        .mosi_io_num = GPIO_NUM_21, .miso_io_num = -1,
        .sclk_io_num = GPIO_NUM_15,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };
    e = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "LCD SPI3 init: %s", esp_err_to_name(e)); return false;
    }
    spi_device_interface_config_t dev = {
        .clock_speed_hz = LCD_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = -1, // CS manual: keep asserted over CMD + parameters
        .queue_size = 2
    };
    e = spi_bus_add_device(LCD_HOST, &dev, &lcd);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "LCD SPI3 device: %s", esp_err_to_name(e)); return false;
    }
    bool ok = BOBER_LCD_ST7735S ? st7735_init() : gc9107_init();
    bool light = init_backlight();
    ESP_LOGI(TAG, "LCD %s on SPI3 mode0 %dMHz manual-CS14 (CMD+params together): init=%s backlight=%s",
        BOBER_LCD_ST7735S ? "ST7735S" : "GC9107", LCD_CLOCK_HZ/1000000,
        ok ? "OK" : "SPI ERROR", light ? "OK" : "ERROR");
    return ok && light;
}

static void fill(uint16_t color)
{
    if (!lcd || !pixels) return;
#if BOBER_LCD_ST7735S
    const uint16_t xoff = 2, yoff = 1;
#else
    const uint16_t xoff = 0, yoff = 32;
#endif
    const uint8_t x[] = {0,(uint8_t)xoff,0,(uint8_t)(xoff+127)};
    const uint8_t y[] = {0,(uint8_t)yoff,0,(uint8_t)(yoff+127)};
    uint8_t command;
    /* One CS assertion across window setup and ALL pixel data. */
    lcd_select();
    command = 0x2a;
    (void)lcd_tx(true, &command, 1); (void)lcd_tx(false, x, sizeof(x));
    command = 0x2b;
    (void)lcd_tx(true, &command, 1); (void)lcd_tx(false, y, sizeof(y));
    command = 0x2c;
    (void)lcd_tx(true, &command, 1);
    for (unsigned i = 0; i < LCD_ROW_BYTES * 4; i += 2) {
        pixels[i] = (uint8_t)(color >> 8);
        pixels[i+1] = (uint8_t)color;
    }
    for (int i = 0; i < 32; ++i)
        (void)lcd_tx(false, pixels, LCD_ROW_BYTES * 4);
    lcd_deselect();
}

#if BOBER_LCD_ANIMATION
/* Reuse the v1.7 manual-CS transport, staging four rows in internal DMA RAM. */
static bool draw_frame(const uint8_t *frame, int dx, int dy, unsigned rotation)
{
    /* Physical panel orientation confirmed on device: compensate by 180 degrees. */
    rotation=(rotation+2u)&3u;
#if BOBER_LCD_ST7735S
    const uint16_t xoff = 2, yoff = 1;
#else
    const uint16_t xoff = 0, yoff = 32;
#endif
    const uint8_t x[] = {0,(uint8_t)xoff,0,(uint8_t)(xoff+127)};
    const uint8_t y[] = {0,(uint8_t)yoff,0,(uint8_t)(yoff+127)};
    uint8_t command=0x2a;
    lcd_select();
    bool ok=lcd_tx(true,&command,1)==ESP_OK;
    if (ok) ok=lcd_tx(false,x,sizeof(x))==ESP_OK;
    command=0x2b;
    if (ok) ok=lcd_tx(true,&command,1)==ESP_OK;
    if (ok) ok=lcd_tx(false,y,sizeof(y))==ESP_OK;
    command=0x2c;
    if (ok) ok=lcd_tx(true,&command,1)==ESP_OK;
    for (size_t row=0;ok && row<128;row+=4) {
        ok=bober_picture_rows_rotated(frame,pixels,LCD_ROW_BYTES*4,(unsigned)row,4,dx,dy,rotation);
        if(ok)ok=lcd_tx(false,pixels,LCD_ROW_BYTES*4)==ESP_OK;
    }
    lcd_deselect();
    return ok;
}
#endif

static void play_animation(void)
{
#if BOBER_LCD_ANIMATION
    extern const uint8_t loop_start[] asm("_binary_loop_banim_start");
    extern const uint8_t loop_end[] asm("_binary_loop_banim_end");
    bober_animation_t animation;
    size_t size=(size_t)((uintptr_t)loop_end-(uintptr_t)loop_start);
    if (!bober_animation_open(&animation,loop_start,size)) {
        ESP_LOGE(TAG,"LCD animation invalid; falling back to status colors");
        return;
    }
    uint8_t *frame=heap_caps_malloc(32768,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!frame) {
        ESP_LOGE(TAG,"LCD animation framebuffer allocation failed; status colors");
        return;
    }
    if (!bober_animation_decode(&animation,0,frame,32768)) {
        heap_caps_free(frame);return;
    }
    ESP_LOGI(TAG,"LCD voice picture; fps=%u frames=%lu",
             (unsigned)animation.fps,(unsigned long)animation.count);
    bool display_on=true;
    lumi_slideshow_t slideshow={0};
    uint32_t displayed=0;
    bober_orientation_t orientation={0};
    /* Show frame zero before loading IMU configuration; audio can start now. */
    if (!draw_frame(frame,0,0,0)) { heap_caps_free(frame); return; }
    g_display_ready=true;
    if(bober_imu_init(internal_bus)) {
        bober_power_register(BOBER_POWER_MOTION);
        if(xTaskCreatePinnedToCore(bober_motion_task,"motion",4096,NULL,5,NULL,1)!=pdPASS){
            bober_power_unregister(BOBER_POWER_MOTION);
            ESP_LOGE(TAG,"Motion task unavailable; fixed display orientation");
        }
    }
    while (true) {
        int64_t tick_started=bober_now_ms();
        if(bober_power_requested()){
            bober_orientation_pause(&orientation);
            if(display_on){
                if(backlight)bl_write(0x0e,0x00);
                lcd_cmd(0x28);lcd_cmd(0x10);delay_ms(120);
                display_on=false;
            }
            bober_power_checkpoint(BOBER_POWER_SCREEN);
            delay_ms(20);continue;
        }
        if(!lumi_display_visible((uint32_t)tick_started)){
            bober_orientation_pause(&orientation);
            if(display_on){if(backlight)bl_write(0x0e,0);lcd_cmd(0x28);lcd_cmd(0x10);delay_ms(120);display_on=false;ESP_LOGI(TAG,"Display timeout: off after 60 s");}
            delay_ms(50);continue;
        }
        bool waking=!display_on;
        if(waking){lcd_cmd(0x11);delay_ms(120);lcd_cmd(0x29);delay_ms(10);}
        int16_t ax,ay,az;
        unsigned old_rotation=orientation.rotation;
        if (bober_motion_latest(&ax,&ay,&az,(uint32_t)tick_started))
            bober_orientation_update(&orientation,ax,ay,az,(uint32_t)tick_started);
        else bober_orientation_pause(&orientation);
        if (old_rotation!=orientation.rotation)
            ESP_LOGI(TAG,"LCD rotation=%u degrees",((orientation.rotation+2u)&3u)*90);
        int dx=0,dy=0;
        bool speaking=(g_session_enabled&&g_ws_ready&&g_phase==BOBER_SPEAKING)||lumi_reaction_busy();
        bool listening=g_session_enabled&&g_ws_ready&&g_phase==BOBER_LISTENING;
        uint8_t level=speaking?g_speaker_level:g_mic_level;
        uint32_t next=lumi_slideshow_update(&slideshow,(uint32_t)tick_started,
                                          speaking||(listening&&level>4)||waking,animation.count);
        if(next!=displayed){
            if(!bober_animation_decode(&animation,next,frame,32768))break;
            displayed=next;
        }
        bober_picture_motion((uint32_t)tick_started,speaking,listening,level,&dx,&dy);
        if(!draw_frame(frame,dx,dy,orientation.rotation))break;
        if(waking&&backlight)bl_write(0x0e,0xff);
        display_on=true;
        g_display_ready=true;
        int64_t remaining=1000/animation.fps-(bober_now_ms()-tick_started);
        delay_ms(remaining>0?(uint32_t)remaining:1);
    }
    ESP_LOGE(TAG,"LCD playback failed; using fallback status colors");
    heap_caps_free(frame);
#endif
}

void bober_screen_task(void *arg)
{
    (void)arg;
    pixels = heap_caps_malloc(LCD_ROW_BYTES * 4, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!pixels) {
        ESP_LOGE(TAG, "LCD DMA memory allocation failed"); bober_power_unregister(BOBER_POWER_SCREEN); vTaskDelete(NULL); return;
    }
    bool ok = lcd_init();
    ESP_LOGI(TAG, "LCD init=%s BGR=%d (no boot color test)",
             ok ? "OK" : "ERROR", BOBER_LCD_BGR);
    if (ok) fill(0x0000);
    if (ok) play_animation();
    g_display_ready=true;
    int prev = -1;
    bool screen_off=false;
    while (true) {
        if(bober_power_requested()){
            if(!screen_off){if(backlight)bl_write(0x0e,0);lcd_cmd(0x28);lcd_cmd(0x10);delay_ms(120);screen_off=true;}
            bober_power_checkpoint(BOBER_POWER_SCREEN);
            delay_ms(20);continue;
        }
        if(!lumi_display_visible((uint32_t)bober_now_ms())){
            if(!screen_off){if(backlight)bl_write(0x0e,0);lcd_cmd(0x28);lcd_cmd(0x10);delay_ms(120);screen_off=true;ESP_LOGI(TAG,"Display timeout: off after 60 s");}
            delay_ms(50);continue;
        }
        if(screen_off){lcd_cmd(0x11);delay_ms(120);lcd_cmd(0x29);if(backlight)bl_write(0x0e,0xff);screen_off=false;prev=-1;}
        int state;
        if (g_phase == BOBER_WAKE_WAIT) state = 5;
        else if (!g_wifi_ready) state = 0;
        else if (!g_session_enabled || g_phase == BOBER_OFF) state = 1;
        else if (!g_ws_ready) state = 2;
        else if (g_phase == BOBER_SPEAKING) state = 4;
        else state = 3;
        if (state != prev) {
            static const uint16_t c[6] = {0xf800, 0x0000, 0xfd20, 0x07e0, 0x001f, 0x07ff};
            fill(c[state]); prev = state;
            const char *names[6] = {"NO_WIFI_RED", "PAUSED_OFF", "CONNECT_AMBER", "LISTEN_GREEN", "SPEAK_BLUE", "WAKE_WAIT_CYAN"};
            ESP_LOGI(TAG, "screen state=%s", names[state]);
        }
        delay_ms(80);
    }
}
