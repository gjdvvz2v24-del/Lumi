#!/usr/bin/env python3
"""Test production Wi-Fi callbacks and codec/I2S shutdown, no ESP32 hardware."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[1]
w=(root/'main/wifi.c').read_text();a=(root/'main/audio.c').read_text()
w=w[w.index('static const char *TAG'):w.index('bool bober_wifi_start')]+w[w.index('bool bober_wifi_suspend'):]
a=a[a.index('bool bober_audio_suspend'):a.index('static inline int16_t clamp')]
prefix=r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_ERR_WIFI_NOT_STARTED 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_CODEC_DEV_OK 0
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define IP_EVENT_STA_GOT_IP 3
#define IP_EVENT_STA_LOST_IP 4
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
typedef int esp_err_t;
typedef int esp_timer_handle_t;
typedef int esp_event_base_t;
static struct {char ssid[33];} g_cfg={"test"};
static bool g_wifi_ready,g_ws_ready,request,timer_active;
static int connects,stops,close_result,disable_result,connect_result,disconnects;
static uint64_t timer_delay;
static int amp=1,codec_dev=1,rx=2,tx=3;
static unsigned audio_sequence;
bool bober_power_requested(void){return request;}
int esp_wifi_connect(void){connects++;return connect_result;}
int esp_wifi_disconnect(void){disconnects++;return 0;}
bool esp_timer_is_active(int handle){assert(handle);return timer_active;}
int esp_timer_start_once(int handle,uint64_t us){assert(handle);timer_delay=us;timer_active=true;return 0;}
void esp_timer_stop(int handle){assert(handle);timer_active=false;}
int esp_wifi_stop(void){stops++;return 0;}
int ext_write(uint8_t reg,uint8_t value){assert(reg==5&&value==0xfe);audio_sequence=1;return 0;}
int esp_codec_dev_close(int handle){assert(handle&&audio_sequence==1);audio_sequence=2;return close_result;}
int i2s_channel_disable(int handle){
 if(handle==2){assert(audio_sequence==2);audio_sequence=3;}
 else {assert(handle==3&&audio_sequence==3);audio_sequence=4;}
 return disable_result;
}
'''
main=r'''
int main(void){
 retry=1;initialized=true;
 handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);assert(connects==1);
 handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(g_wifi_ready);
 handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);assert(timer_active&&!g_wifi_ready);
 timer_active=false;retry_cb(NULL);assert(connects==2&&timer_delay==30000000);
 // Missing driver event / missing DHCP must not strand the connection.
 timer_active=false;retry_cb(NULL);assert(disconnects==1&&timer_delay==3000000);
 connect_result=-1;timer_active=false;retry_cb(NULL);
 assert(connects==3&&timer_active&&timer_delay==3000000&&!attempt_pending);
 timer_active=false;retry_cb(NULL);assert(connects==4&&timer_active);
 connect_result=0;timer_active=false;retry_cb(NULL);assert(connects==5);
 handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(g_wifi_ready&&!timer_active);
 retry_cb(NULL);assert(connects==5);
 handler(NULL,IP_EVENT,IP_EVENT_STA_LOST_IP,NULL);assert(!g_wifi_ready&&timer_active);
 timer_active=false;retry_cb(NULL);assert(disconnects==2);
 timer_active=false;retry_cb(NULL);assert(connects==6);

 request=true;g_wifi_ready=g_ws_ready=true;
 assert(bober_wifi_suspend());assert(stops==1&&!timer_active&&!g_wifi_ready&&!g_ws_ready);
 retry_cb(NULL);handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);
 handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
 handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
 assert(connects==6&&!timer_active&&!g_wifi_ready);
 initialized=false;assert(bober_wifi_suspend());assert(stops==1);
 assert(bober_audio_suspend()&&audio_sequence==4);
 disable_result=ESP_ERR_INVALID_STATE;assert(bober_audio_suspend());
 close_result=-1;assert(!bober_audio_suspend());
 close_result=0;disable_result=-1;assert(!bober_audio_suspend());
 puts("PASS: no reconnect/IP publication during sleep, retry cancellation, amplifier/codec/I2S shutdown order and failures");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(prefix+w+a+main);binary=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
