#include "bober.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
static const char *TAG="button";
void bober_button_task(void *arg)
{
 (void)arg;
 gpio_config_t c={.pin_bit_mask=1ULL<<41,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE};
 gpio_config(&c);
 /* Consume the wake press, even if held across the restart. */
 while(gpio_get_level(GPIO_NUM_41)==0)vTaskDelay(pdMS_TO_TICKS(10));
 bober_button_state_t button={0};
 ESP_LOGI(TAG,"Click: return to Hej Lumi wait; hold 1.2s: light sleep");
 while(1){
  bober_button_event_t event=bober_button_update(&button,gpio_get_level(GPIO_NUM_41)==0,(uint32_t)bober_now_ms());
  if(event==BOBER_BUTTON_HOLD)bober_power_sleep();
  else if(event==BOBER_BUTTON_CLICK)esp_restart();
  vTaskDelay(pdMS_TO_TICKS(10));
 }
}
