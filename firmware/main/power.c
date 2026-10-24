#include "bober.h"
#include "power.h"
#include "imu.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_log.h"
#include <stdatomic.h>
static const char *TAG="power";
static EventGroupHandle_t parked;
static atomic_bool requested;
static atomic_uint required;
static RTC_NOINIT_ATTR uint32_t wake_cookie,wake_inverse;
#define WAKE_COOKIE 0x424f4252u
bool bober_power_woke(void){
 bool woke=esp_reset_reason()==ESP_RST_SW&&wake_cookie==WAKE_COOKIE&&wake_inverse==~WAKE_COOKIE;
 wake_cookie=wake_inverse=0;return woke;
}
bool bober_power_init(void){
 parked=xEventGroupCreate();
 atomic_store(&required,BOBER_POWER_MIC|BOBER_POWER_SPEAKER|BOBER_POWER_NETWORK|BOBER_POWER_UPLINK|BOBER_POWER_SCREEN);
 atomic_store(&requested,false);return parked!=NULL;
}
bool bober_power_requested(void){return atomic_load(&requested);}
void bober_power_register(uint32_t bit){atomic_fetch_or(&required,bit);}
void bober_power_unregister(uint32_t bit){atomic_fetch_and(&required,~bit);}
void bober_power_checkpoint(uint32_t bit){
 if(!bober_power_requested())return;
 xEventGroupSetBits(parked,bit);
 /* No task is suspended while holding I2C, I2S, PCM or websocket locks. */
 for(;;)vTaskSuspend(NULL);
}
static void wait_release(void){
 unsigned released=0;
 while(released<4){
  released=gpio_get_level(GPIO_NUM_41)?released+1:0;
  vTaskDelay(pdMS_TO_TICKS(10));
 }
}
static void restart_voice(void){
 wait_release();
 wake_cookie=WAKE_COOKIE;wake_inverse=~WAKE_COOKIE;
 esp_restart();
}
void bober_power_sleep(void){
 if(!parked)return;
 atomic_store(&requested,true);
 /* Stop voice producers before parking tasks and switching off peripherals. */
 g_session_enabled=false;g_generation++;g_ws_ready=false;g_phase=BOBER_OFF;
 bool wifi_ok=bober_wifi_suspend();
 int64_t deadline=bober_now_ms()+45000;
 bool released=false;unsigned high_samples=0;
 while((xEventGroupGetBits(parked)&atomic_load(&required))!=atomic_load(&required)){
  int high=gpio_get_level(GPIO_NUM_41);
  high_samples=high?high_samples+1:0;if(high_samples>=4)released=true;
  if(released&&!high){ESP_LOGI(TAG,"Sleep cancelled by button");restart_voice();return;}
  if(bober_now_ms()>=deadline){ESP_LOGE(TAG,"Sleep preparation timed out; restarting voice");restart_voice();return;}
  vTaskDelay(pdMS_TO_TICKS(10));
 }
 /* All users of these buses are now parked, including optional motion task. */
 bool audio_ok=bober_audio_suspend();
 bool imu_ok=bober_imu_suspend();
 if(!wifi_ok||!audio_ok||!imu_ok){ESP_LOGE(TAG,"Peripheral shutdown failed; restarting voice");restart_voice();return;}
 wait_release(); /* the original long press must never immediately wake us */
 esp_err_t e=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
 if(e==ESP_OK)e=gpio_wakeup_enable(GPIO_NUM_41,GPIO_INTR_LOW_LEVEL);
 if(e==ESP_OK)e=esp_sleep_enable_gpio_wakeup();
 if(e!=ESP_OK){ESP_LOGE(TAG,"Button wake configuration failed: %s",esp_err_to_name(e));restart_voice();return;}
 ESP_LOGI(TAG,"LIGHT_SLEEP: Wi-Fi/audio/IMU off; wake with front button GPIO41");
 /* No timer wakeup. Flash and PSRAM remain retained using IDF default power policy. */
 do {
  e=esp_light_sleep_start();
 } while(e==ESP_OK&&esp_sleep_get_wakeup_cause()!=ESP_SLEEP_WAKEUP_GPIO&&gpio_get_level(GPIO_NUM_41));
 if(e!=ESP_OK)ESP_LOGW(TAG,"Light sleep returned %s",esp_err_to_name(e));
 restart_voice();
}
