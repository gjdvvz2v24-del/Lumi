#include "bober.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
static const char *TAG="wifi";static esp_timer_handle_t retry;
static bool initialized;
static volatile bool attempt_pending;
static void schedule_retry(uint64_t delay_us){
 if(!retry||bober_power_requested())return;
 esp_timer_stop(retry);
 esp_err_t e=esp_timer_start_once(retry,delay_us);
 if(e!=ESP_OK)ESP_LOGE(TAG,"Wi-Fi retry timer failed: %s",esp_err_to_name(e));
}
static void connect_wifi(void){
 if(bober_power_requested()||g_wifi_ready||!g_cfg.ssid[0])return;
 ESP_LOGI(TAG,"Wi-Fi connection attempt");
 attempt_pending=true;
 /* Keep a watchdog even when the driver never reports a disconnect/IP event. */
 schedule_retry(30000000);
 esp_err_t e=esp_wifi_connect();
 if(e!=ESP_OK){
  attempt_pending=false;
  ESP_LOGW(TAG,"Wi-Fi connect failed: %s; retry in 3 s",esp_err_to_name(e));
  schedule_retry(3000000);
 }
}
static void retry_cb(void*a){
 (void)a;
 if(bober_power_requested()||g_wifi_ready||!g_cfg.ssid[0])return;
 if(attempt_pending){
  attempt_pending=false;
  ESP_LOGW(TAG,"Wi-Fi/IP timeout; reset connection and retry in 3 s");
  schedule_retry(3000000);
  esp_wifi_disconnect();
  return;
 }
 connect_wifi();
}
static void handler(void *a,esp_event_base_t base,int32_t event,void *data){(void)a;(void)data;
 if(base==WIFI_EVENT&&event==WIFI_EVENT_STA_START){connect_wifi();}
 else if(base==WIFI_EVENT&&event==WIFI_EVENT_STA_DISCONNECTED){
  g_wifi_ready=false;g_ws_ready=false;attempt_pending=false;
  if(bober_power_requested())return;
  ESP_LOGW(TAG,"Wi-Fi disconnected; reconnect in 3 s");schedule_retry(3000000);
 }else if(base==IP_EVENT&&event==IP_EVENT_STA_LOST_IP){
  g_wifi_ready=false;g_ws_ready=false;
  /* Association can survive a lost DHCP lease: restart it on the next retry. */
  attempt_pending=true;schedule_retry(3000000);
 }else if(base==IP_EVENT&&event==IP_EVENT_STA_GOT_IP){
  if(bober_power_requested())return;
  attempt_pending=false;if(retry)esp_timer_stop(retry);
  g_wifi_ready=true;ESP_LOGI(TAG,"STA IP ready; OpenAI may connect");
 }
}
bool bober_wifi_start(void){
 if(!g_cfg.ssid[0])return false;
 ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());esp_netif_create_default_wifi_sta();
 wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));initialized=true;
 ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,handler,NULL));
 ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,ESP_EVENT_ANY_ID,handler,NULL));
 esp_timer_create_args_t arg={.callback=retry_cb,.name="wifi_retry"};ESP_ERROR_CHECK(esp_timer_create(&arg,&retry));
 ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
 ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
 wifi_config_t c={0};strlcpy((char*)c.sta.ssid,g_cfg.ssid,sizeof(c.sta.ssid));strlcpy((char*)c.sta.password,g_cfg.password,sizeof(c.sta.password));
 ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&c));ESP_ERROR_CHECK(esp_wifi_start());esp_wifi_set_ps(WIFI_PS_NONE);return true;
}

bool bober_wifi_suspend(void){
 g_wifi_ready=false;g_ws_ready=false;
 if(retry&&esp_timer_is_active(retry))esp_timer_stop(retry);
 if(!initialized)return true;
 esp_err_t e=esp_wifi_stop();
 if(retry&&esp_timer_is_active(retry))esp_timer_stop(retry);
 return e==ESP_OK||e==ESP_ERR_WIFI_NOT_STARTED;
}
