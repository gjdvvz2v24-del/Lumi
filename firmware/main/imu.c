#include "imu.h"
#include "bmi270_config.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG="imu";
static i2c_master_dev_handle_t imu;
static unsigned errors;
static bool ready;
_Static_assert(sizeof(bober_bmi270_config)==8192, "BMI270 configuration size");

static void delay_ms(unsigned ms)
{
    TickType_t ticks=pdMS_TO_TICKS(ms);
    vTaskDelay(ticks?ticks:1);
}
static bool write_reg(uint8_t reg, const uint8_t *data, size_t size)
{
    uint8_t packet[129];
    if (!imu || size>128) return false;
    packet[0]=reg; memcpy(packet+1,data,size);
    return i2c_master_transmit(imu,packet,size+1,20)==ESP_OK;
}
static bool set_reg(uint8_t reg,uint8_t value) { return write_reg(reg,&value,1); }
static bool read_reg(uint8_t reg,uint8_t *data,size_t size)
{
    return imu && i2c_master_transmit_receive(imu,&reg,1,data,size,10)==ESP_OK;
}
bool bober_imu_init(i2c_master_bus_handle_t bus)
{
    ready=false;
    if (!bus || i2c_master_probe(bus,0x68,20)!=ESP_OK) goto fail;
    i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,
                            .device_address=0x68,.scl_speed_hz=400000};
    if (i2c_master_bus_add_device(bus,&cfg,&imu)!=ESP_OK) goto fail;
    uint8_t id=0,status=0;
    if (!read_reg(0x00,&id,1)||id!=0x24) goto fail;
    if (!set_reg(0x7e,0xb6)) goto fail; /* soft reset */
    delay_ms(5);
    if (!set_reg(0x7c,0x00)) goto fail; /* advanced power save off */
    delay_ms(2);
    if (!set_reg(0x59,0x00)) goto fail;
    for (size_t pos=0;pos<sizeof(bober_bmi270_config);pos+=128) {
        uint8_t address[2]={(uint8_t)((pos/2)&15),(uint8_t)(pos/32)};
        if (!write_reg(0x5b,address,2)||
            !write_reg(0x5e,bober_bmi270_config+pos,128)) goto fail;
    }
    if (!set_reg(0x59,0x01)) goto fail;
    /* Bosch specifies waiting for internal configuration processing. */
    delay_ms(150);
    if (!read_reg(0x21,&status,1)||(status&15)!=1) goto fail;
    /* Accelerometer only: normal bandwidth, 100 Hz, +/-8g for impact capture. */
    if (!set_reg(0x40,0xa8)||!set_reg(0x41,0x02)||!set_reg(0x7d,0x04)) goto fail;
    delay_ms(25);
    if (!read_reg(0x02,&status,1)||(status&0x1f)) goto fail;
    errors=0; ready=true;
    ESP_LOGI(TAG,"BMI270 ready: rotation + strong shake wake, accel 100Hz +/-8g");
    return true;
fail:
    ESP_LOGW(TAG,"BMI270 unavailable; keeping fixed screen orientation");
    if (imu) { i2c_master_bus_rm_device(imu); imu=NULL; }
    return false;
}
bool bober_imu_suspend(void)
{
    if(!imu)return true;
    ready=false;
    bool ok=set_reg(0x7d,0x00); /* disable accel, gyro, auxiliary sensor and temperature */
    ok=set_reg(0x7c,0x01)&&ok; /* advanced power save */
    return ok;
}
bool bober_imu_read(int16_t *x,int16_t *y,int16_t *z)
{
    if (!ready||!x||!y||!z) return false;
    uint8_t data[6];
    if (!read_reg(0x0c,data,sizeof(data))) {
        if (++errors>=3) {
            ready=false;
            ESP_LOGW(TAG,"BMI270 read failed; holding last rotation until restart");
        }
        return false;
    }
    errors=0;
    *x=(int16_t)((uint16_t)data[0]|((uint16_t)data[1]<<8));
    *y=(int16_t)((uint16_t)data[2]|((uint16_t)data[3]<<8));
    *z=(int16_t)((uint16_t)data[4]|((uint16_t)data[5]<<8));
    return true;
}
