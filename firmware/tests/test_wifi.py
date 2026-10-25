#!/usr/bin/env python3
"""Execute production Wi-Fi selection, retries and profile loading with fake radio/NVS."""
import os, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
idf=Path(os.environ.get('IDF_PATH','/Users/grpi/Work/lumi/esp-idf'))
source=(root/'main/wifi.c').read_text()
body=source[source.index('static const char *TAG'):source.index('bool bober_wifi_start')]+source[source.index('bool bober_wifi_suspend'):]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_profiles.h"
#define ESP_OK 0
#define ESP_ERR_WIFI_NOT_STARTED 2
#define ESP_ERR_WIFI_STATE 3
#define WIFI_IF_STA 0
#define WIFI_AUTH_OPEN 0
#define WIFI_EVENT 1
#define IP_EVENT 2
#define LUMI_WIFI_CONTROL 3
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define WIFI_EVENT_SCAN_DONE 3
#define IP_EVENT_STA_GOT_IP 4
#define IP_EVENT_STA_LOST_IP 5
#define NVS_READONLY 0
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
typedef int esp_err_t;
typedef int esp_timer_handle_t;
typedef int esp_event_base_t;
typedef int nvs_handle_t;
typedef struct {uint32_t status;} wifi_event_sta_scan_done_t;
typedef struct {uint8_t ssid[33],bssid[6],primary;int8_t rssi;int authmode;} wifi_ap_record_t;
typedef struct {struct {uint8_t ssid[32],password[64],bssid[6],channel;bool bssid_set;} sta;} wifi_config_t;
static struct {char ssid[33],password[65];} g_cfg={"legacy","legacy-pass"};
static bool request,g_wifi_ready,g_ws_ready,timer_active;
static int scans,scan_stops,connects,disconnects,stops,clears,connect_result,scan_result,config_result;
static uint64_t timer_delay;
static int64_t clock_ms=1000;
static wifi_config_t last_config;
static wifi_ap_record_t seen[8];static uint16_t seen_count;
static const char *stored;
static void handler(void*,esp_event_base_t,int32_t,void*);
static bool bober_power_requested(void){return request;}
static int64_t bober_now_ms(void){return clock_ms;}
static const char *esp_err_to_name(int e){(void)e;return "fake error";}
static int esp_wifi_scan_start(const void *cfg,bool block){assert(!cfg&&!block);scans++;return scan_result;}
static int esp_wifi_scan_stop(void){scan_stops++;return 0;}
static int esp_wifi_clear_ap_list(void){clears++;return 0;}
static int esp_wifi_scan_get_ap_num(uint16_t *n){*n=seen_count;return 0;}
static int esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *aps){assert(*n==seen_count);memcpy(aps,seen,*n*sizeof(*aps));clears++;return 0;}
static int esp_wifi_set_config(int iface,const wifi_config_t *cfg){assert(!iface);last_config=*cfg;return config_result;}
static int esp_wifi_connect(void){connects++;return connect_result;}
static int esp_wifi_disconnect(void){disconnects++;return 0;}
static bool esp_timer_is_active(int h){assert(h);return timer_active;}
static int esp_timer_start_once(int h,uint64_t us){assert(h);timer_delay=us;timer_active=true;return 0;}
static void esp_timer_stop(int h){assert(h);timer_active=false;}
static int esp_wifi_stop(void){stops++;return 0;}
static int esp_event_post(int base,int event,const void *data,size_t len,int wait){(void)data;assert(!len&&!wait);handler(NULL,base,event,NULL);return 0;}
static int nvs_open(const char *ns,int mode,nvs_handle_t *n){assert(!strcmp(ns,"bober")&&mode==NVS_READONLY);*n=1;return stored?0:-1;}
static int nvs_get_str(int n,const char *key,char *out,size_t *size){assert(n==1&&!strcmp(key,"wifi_profiles"));if(strlen(stored)+1>*size)return -1;strcpy(out,stored);return 0;}
static void nvs_close(int n){assert(n==1);}
'''
main=r'''
static void ap(int i,const char *ssid,int rssi,int id,int auth){
 memset(&seen[i],0,sizeof(seen[i]));strlcpy((char*)seen[i].ssid,ssid,sizeof(seen[i].ssid));seen[i].rssi=rssi;seen[i].bssid[5]=id;seen[i].primary=6;seen[i].authmode=auth;
}
static void done(void){wifi_event_sta_scan_done_t event={0};handler(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&event);}
static void tick(void){timer_active=false;retry_cb(NULL);}
int main(void){
 retry=1;initialized=true;
 load_profiles();assert(!strcmp(profiles[0].ssid,"legacy")&&!profiles[1].ssid[0]);
 stored="bad";load_profiles();assert(!strcmp(profiles[0].ssid,"legacy"));
 stored="[{\"ssid\":\"one\",\"password\":\"password1\"},{\"ssid\":\"two\",\"password\":\"password2\"},{\"ssid\":\"three\",\"password\":\"password3\"}]";
 load_profiles();assert(!strcmp(profiles[2].ssid,"three"));
 seen_count=5;ap(0,"unknown",-10,1,1);ap(1,"one",-65,2,1);ap(2,"two",-35,3,1);ap(3,"two",-25,4,1);ap(4,"three",-45,5,1);
 handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);assert(scans==1&&connects==0&&scan_pending&&timer_delay==10000000);
 done();assert(connects==1&&active_profile==1&&attempt_pending&&!scan_pending&&timer_delay==30000000);
 assert(!memcmp(last_config.sta.ssid,"two",3)&&last_config.sta.bssid_set&&last_config.sta.bssid[5]==4);
 handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(g_wifi_ready&&!timer_active&&!attempt_pending);
 tick();assert(scans==1); /* Healthy connection does not roam or interrupt audio. */
 handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);assert(!g_wifi_ready&&!g_ws_ready&&timer_delay==3000000);
 seen[4].rssi=-15;tick();assert(scans==2);done();assert(connects==2&&active_profile==2);
 /* Failed authentication is cooled down, so the strongest bad profile cannot starve alternatives. */
 handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);assert(failed_until[2]==clock_ms+30000);
 tick();done();assert(connects==3&&active_profile==1);
 tick();assert(disconnects==1&&!attempt_pending&&timer_delay==3000000); /* DHCP watchdog */
 tick();done();assert(connects==4&&active_profile==0);
 handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(g_wifi_ready);
 handler(NULL,IP_EVENT,IP_EVENT_STA_LOST_IP,NULL);tick();assert(disconnects==2&&!g_wifi_ready);
 clock_ms+=31000;tick();done();assert(active_profile==2); /* Failed profiles become eligible again. */
 handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
 handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
 /* Immediate connect failures also rescan; scan errors, no networks and missing SCAN_DONE recover. */
 connect_result=-1;tick();done();assert(!attempt_pending&&timer_delay==3000000);connect_result=0;
 seen_count=0;int previous=connects;tick();done();assert(connects==previous&&timer_active);
 scan_result=-1;tick();assert(!scan_pending&&timer_delay==3000000);
 scan_result=ESP_ERR_WIFI_STATE;int old_disconnects=disconnects;tick();assert(disconnects==old_disconnects+1&&!scan_pending);scan_result=0;
 tick();assert(scan_pending);tick();assert(!scan_pending&&scan_stops==1&&timer_active);done();assert(connects==previous);
 /* Equal RSSI prefers the earlier configured profile; open lookalikes are excluded. */
 memset(failed_until,0,sizeof(failed_until));seen_count=3;ap(0,"two",-20,1,1);ap(1,"one",-20,2,1);ap(2,"three",-10,3,WIFI_AUTH_OPEN);
 tick();done();assert(active_profile==0&&last_config.sta.bssid[5]==2);
 handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
 /* Driver config failure does not publish an in-flight connect. */
 config_result=-1;tick();previous=connects;done();assert(connects==previous&&!attempt_pending&&timer_active);config_result=0;
 /* Maximum-length SSID and password are copied without truncation. */
 memset(profiles,0,sizeof(profiles));memset(profiles[0].ssid,'s',32);memset(profiles[0].password,'p',63);
 seen_count=1;ap(0,profiles[0].ssid,-30,6,1);tick();done();assert(!memcmp(last_config.sta.ssid,profiles[0].ssid,32)&&strlen((char*)last_config.sta.password)==63);
 request=true;g_wifi_ready=g_ws_ready=true;int before=scans;
 assert(bober_wifi_suspend()&&stops==1&&!g_wifi_ready&&!g_ws_ready&&!timer_active);
 tick();handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);done();handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
 assert(scans==before&&!g_wifi_ready&&!timer_active);
 initialized=false;assert(bober_wifi_suspend()&&stops==1);
 assert(clears>0);puts("PASS: legacy/3-profile loading, strongest BSSID, disconnect rescan, auth cooldown, DHCP/scan watchdogs, empty/error scans, ties/security, max lengths, sleep");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(prefix+body+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(root/'main'),'-I'+str(idf/'components/json/cJSON'),str(p/'test.c'),str(idf/'components/json/cJSON/cJSON.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
