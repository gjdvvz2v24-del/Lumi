#include "bober.h"
#include "wifi_profiles.h"
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
ESP_EVENT_DEFINE_BASE(LUMI_WIFI_CONTROL);
static const char *TAG="wifi";static esp_timer_handle_t retry;
static bool initialized;
static volatile bool attempt_pending,scan_pending;
static lumi_wifi_profile_t profiles[LUMI_WIFI_NETWORKS];
static int active_profile=-1;
static int64_t failed_until[LUMI_WIFI_NETWORKS];
static void schedule_retry(uint64_t delay_us){
 if(!retry||bober_power_requested())return;
 esp_timer_stop(retry);
 esp_err_t e=esp_timer_start_once(retry,delay_us);
 if(e!=ESP_OK)ESP_LOGE(TAG,"Wi-Fi retry timer failed: %s",esp_err_to_name(e));
}
static void mark_failed(void){
 if(active_profile>=0)failed_until[active_profile]=bober_now_ms()+30000;
 active_profile=-1;attempt_pending=false;
}
/* Asynchronous scan: never block the event loop or the timer task. */
static void connect_wifi(void){
 if(bober_power_requested()||g_wifi_ready||scan_pending||attempt_pending||!profiles[0].ssid[0])return;
 ESP_LOGI(TAG,"Scanning configured Wi-Fi networks");
 scan_pending=true;schedule_retry(10000000);
 esp_err_t e=esp_wifi_scan_start(NULL,false);
 if(e!=ESP_OK){
  scan_pending=false;
  if(e==ESP_ERR_WIFI_STATE)esp_wifi_disconnect();
  ESP_LOGW(TAG,"Wi-Fi scan failed: %s; retry in 3 s",esp_err_to_name(e));schedule_retry(3000000);
 }
}
static void scan_done(const wifi_event_sta_scan_done_t *event){
 if(!scan_pending||bober_power_requested()){
  esp_wifi_clear_ap_list();return;
 }
 scan_pending=false;if(retry)esp_timer_stop(retry);
 uint16_t count=0;
 if(!event||event->status||esp_wifi_scan_get_ap_num(&count)!=ESP_OK||!count){
  esp_wifi_clear_ap_list();schedule_retry(3000000);return;
 }
 wifi_ap_record_t *aps=calloc(count,sizeof(*aps));
 if(!aps){esp_wifi_clear_ap_list();schedule_retry(3000000);return;}
 if(esp_wifi_scan_get_ap_records(&count,aps)!=ESP_OK){
  free(aps);esp_wifi_clear_ap_list();schedule_retry(3000000);return;
 }
 int selected=-1,best_ap=-1,best_rssi=-128;int64_t now=bober_now_ms();
 for(int p=0;p<LUMI_WIFI_NETWORKS;p++){
  size_t len=strlen(profiles[p].ssid);
  if(!len||now<failed_until[p])continue;
  for(uint16_t i=0;i<count;i++){
   size_t seen=strnlen((const char*)aps[i].ssid,sizeof(aps[i].ssid));
   bool open=aps[i].authmode==WIFI_AUTH_OPEN;
   if(seen==len&&!memcmp(aps[i].ssid,profiles[p].ssid,len)&&
      open==!profiles[p].password[0]&&aps[i].rssi>best_rssi){
    selected=p;best_ap=i;best_rssi=aps[i].rssi;
   }
  }
 }
 if(selected<0){free(aps);ESP_LOGW(TAG,"No usable configured Wi-Fi visible; rescan in 3 s");schedule_retry(3000000);return;}
 wifi_config_t cfg={0};
 /* SSIDs may use all 32 bytes; the driver does not require a trailing NUL. */
 memcpy(cfg.sta.ssid,profiles[selected].ssid,strlen(profiles[selected].ssid));
 memcpy(cfg.sta.password,profiles[selected].password,strlen(profiles[selected].password));
 cfg.sta.bssid_set=true;memcpy(cfg.sta.bssid,aps[best_ap].bssid,6);cfg.sta.channel=aps[best_ap].primary;
 free(aps);
 if(bober_power_requested())return;
 esp_err_t e=esp_wifi_set_config(WIFI_IF_STA,&cfg);
 if(e!=ESP_OK){schedule_retry(3000000);return;}
 active_profile=selected;attempt_pending=true;schedule_retry(30000000);
 ESP_LOGI(TAG,"Connecting Wi-Fi profile=%d RSSI=%d dBm",selected+1,best_rssi);
 e=esp_wifi_connect();
 if(e!=ESP_OK){mark_failed();ESP_LOGW(TAG,"Wi-Fi connect failed: %s; rescan in 3 s",esp_err_to_name(e));schedule_retry(3000000);}
}
static void retry_connection(void){
 if(bober_power_requested()||g_wifi_ready||!profiles[0].ssid[0])return;
 if(scan_pending){
  scan_pending=false;esp_wifi_scan_stop();esp_wifi_clear_ap_list();schedule_retry(3000000);return;
 }
 if(attempt_pending){
  mark_failed();ESP_LOGW(TAG,"Wi-Fi/IP timeout; disconnect and rescan in 3 s");
  schedule_retry(3000000);esp_wifi_disconnect();return;
 }
 connect_wifi();
}
static void retry_cb(void *a){
 (void)a;
 if(bober_power_requested())return;
 /* Serialize connection state with driver events on the default event loop. */
 if(esp_event_post(LUMI_WIFI_CONTROL,1,NULL,0,0)!=ESP_OK)schedule_retry(3000000);
}
static void handler(void *a,esp_event_base_t base,int32_t event,void *data){
 (void)a;
 if(base==LUMI_WIFI_CONTROL){retry_connection();}
 else if(base==WIFI_EVENT&&event==WIFI_EVENT_STA_START){connect_wifi();}
 else if(base==WIFI_EVENT&&event==WIFI_EVENT_SCAN_DONE){scan_done(data);}
 else if(base==WIFI_EVENT&&event==WIFI_EVENT_STA_DISCONNECTED){
  g_wifi_ready=false;g_ws_ready=false;
  if(attempt_pending)mark_failed();
  if(bober_power_requested())return;
  ESP_LOGW(TAG,"Wi-Fi disconnected; rescan in 3 s");if(!scan_pending)schedule_retry(3000000);
 }else if(base==IP_EVENT&&event==IP_EVENT_STA_LOST_IP){
  g_wifi_ready=false;g_ws_ready=false;
  /* Association can survive a lost DHCP lease: disconnect before scanning. */
  attempt_pending=true;schedule_retry(3000000);
 }else if(base==IP_EVENT&&event==IP_EVENT_STA_GOT_IP){
  if(bober_power_requested()||(!attempt_pending&&!g_wifi_ready))return;
  if(active_profile>=0)failed_until[active_profile]=0;
  attempt_pending=false;if(retry)esp_timer_stop(retry);
  g_wifi_ready=true;ESP_LOGI(TAG,"STA IP ready; OpenAI may connect");
 }
}
static void load_profiles(void){
 memset(profiles,0,sizeof(profiles));
 strlcpy(profiles[0].ssid,g_cfg.ssid,sizeof(profiles[0].ssid));
 strlcpy(profiles[0].password,g_cfg.password,sizeof(profiles[0].password));
 nvs_handle_t n;
 if(nvs_open("bober",NVS_READONLY,&n)!=ESP_OK)return;
 char *json=malloc(LUMI_WIFI_JSON_MAX+1);size_t size=LUMI_WIFI_JSON_MAX+1;
 if(json&&nvs_get_str(n,"wifi_profiles",json,&size)==ESP_OK&&!lumi_wifi_profiles_parse(json,profiles))
  ESP_LOGW(TAG,"Invalid Wi-Fi profiles; using legacy network");
 if(json){memset(json,0,LUMI_WIFI_JSON_MAX+1);free(json);}nvs_close(n);
}
bool bober_wifi_start(void){
 load_profiles();if(!profiles[0].ssid[0])return false;
 ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());esp_netif_create_default_wifi_sta();
 wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));initialized=true;
 ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,handler,NULL));
 ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,ESP_EVENT_ANY_ID,handler,NULL));
 ESP_ERROR_CHECK(esp_event_handler_register(LUMI_WIFI_CONTROL,ESP_EVENT_ANY_ID,handler,NULL));
 esp_timer_create_args_t arg={.callback=retry_cb,.name="wifi_retry"};ESP_ERROR_CHECK(esp_timer_create(&arg,&retry));
 ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
 ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
 ESP_ERROR_CHECK(esp_wifi_start());esp_wifi_set_ps(WIFI_PS_NONE);return true;
}
bool bober_wifi_suspend(void){
 g_wifi_ready=false;g_ws_ready=false;scan_pending=false;attempt_pending=false;
 if(retry&&esp_timer_is_active(retry))esp_timer_stop(retry);
 if(!initialized)return true;
 esp_err_t e=esp_wifi_stop();
 if(retry&&esp_timer_is_active(retry))esp_timer_stop(retry);
 return e==ESP_OK||e==ESP_ERR_WIFI_NOT_STARTED;
}
