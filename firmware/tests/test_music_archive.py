#!/usr/bin/env python3
"""Execute real Assets uploader: WAV bytes, bounded chunks, retained pending file and deduplication."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
idf=Path(os.environ.get('IDF_PATH','/Users/grpi/Work/lumi/esp-idf'))
s=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "music_protocol.h"
#define ESP_OK 0
#define HTTP_METHOD_GET 0
#define HTTP_METHOD_POST 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdMS_TO_TICKS(x) (x)
static const char *TAG="test";static char log_line[512];
#define ESP_LOGI(tag,fmt,...) ((void)(tag),(void)snprintf(log_line,sizeof(log_line),fmt,##__VA_ARGS__))
#define ESP_LOGW(tag,fmt,...) ESP_LOGI(tag,fmt,##__VA_ARGS__)
#define ESP_LOGE(tag,fmt,...) ESP_LOGI(tag,fmt,##__VA_ARGS__)
typedef int esp_http_client_handle_t;
typedef struct {const char *url;int method,timeout_ms,buffer_size;void *crt_bundle_attach;bool disable_auto_redirect;} esp_http_client_config_t;
static void *esp_crt_bundle_attach;
static struct {char api_key[32];} g_cfg={"fake-key"};
static bool g_wifi_ready=true,power,alloc_fail,write_fail;
static int64_t clock_ms;
static int method,opens,posts,get_count,status=200;
static size_t written,expected,read_pos;
static char body[LUMI_MUSIC_BYTES+2048],reply[9000]="{\"asset_id\":\"asset_ok\"}";
static const char *str(cJSON *j,const char *key){cJSON *v=cJSON_GetObjectItem(j,key);return cJSON_IsString(v)?v->valuestring:"";}
static uint32_t esp_random(void){return 42;}
static int64_t bober_now_ms(void){return clock_ms;}
static bool bober_power_requested(void){return power;}
static void vTaskDelay(int ms){clock_ms+=ms;}
static void *heap_caps_malloc(size_t n,int caps){assert(n==LUMI_MUSIC_BYTES+44&&caps==3);return alloc_fail?NULL:malloc(n);}
static int esp_http_client_init(esp_http_client_config_t *c){assert(c->timeout_ms==15000&&c->disable_auto_redirect);assert(strstr(c->url,"https://api.elevenlabs.io/v1/assets"));method=c->method;return 1;}
static void esp_http_client_set_header(int h,const char *k,const char *v){assert(h&&k&&v);}
static int esp_http_client_open(int h,size_t n){assert(h);opens++;written=read_pos=0;expected=n;if(method==HTTP_METHOD_POST)posts++;else get_count++;return 0;}
static int esp_http_client_write(int h,const char *p,size_t n){assert(h&&n&&n<=4096);if(write_fail)return -1;if(n>137)n=137;assert(written+n<sizeof(body));memcpy(body+written,p,n);written+=n;return (int)n;}
static int esp_http_client_fetch_headers(int h){assert(h);if(method==HTTP_METHOD_POST)assert(written==expected);return 0;}
static int esp_http_client_get_status_code(int h){assert(h);return status;}
static int esp_http_client_read(int h,char *p,size_t cap){assert(h);size_t n=strlen(reply)-read_pos;if(n>cap)n=cap;if(n>17)n=17;memcpy(p,reply+read_pos,n);read_pos+=n;return (int)n;}
static bool esp_http_client_is_complete_data_received(int h){assert(h);return read_pos==strlen(reply);}
static void esp_http_client_close(int h){assert(h);}
static void esp_http_client_cleanup(int h){assert(h);}
#include "music_archive.inc"
static void fill_song(void){
 assert(music_archive_begin());
 uint8_t pcm[1000];memset(pcm,0x5a,sizeof(pcm));
 for(size_t sent=0;sent<LUMI_MUSIC_BYTES;){size_t n=LUMI_MUSIC_BYTES-sent;if(n>sizeof(pcm))n=sizeof(pcm);assert(music_archive_append(pcm,n));sent+=n;}
 assert(!music_archive_append(pcm,2));music_archive_ready=true;
}
int main(void){
 alloc_fail=true;assert(!music_archive_begin()&&!music_archive_wav);alloc_fail=false;
 fill_song();assert(!music_archive_begin()&&music_archive_blocked);music_archive_tick();
 assert(!music_archive_wav&&posts==1&&get_count==0);
 const char *wav=strstr(body,"RIFF");assert(wav&&wav+44+LUMI_MUSIC_BYTES<body+written);
 assert(!memcmp(wav+8,"WAVEfmt ",8)&&!memcmp(wav+36,"data",4));
 assert((uint8_t)wav[20]==1&&(uint8_t)wav[22]==1&&(uint8_t)wav[34]==16);
 uint32_t bytes=(uint8_t)wav[40]|((uint32_t)(uint8_t)wav[41]<<8)|((uint32_t)(uint8_t)wav[42]<<16)|((uint32_t)(uint8_t)wav[43]<<24);
 assert(bytes==1920000);for(unsigned i=0;i<bytes;i++)assert((uint8_t)wav[44+i]==0x5a);
 assert(strstr(body,"name=\"asset\"")&&strstr(body,"Content-Type: audio/wav"));
 fill_song();status=503;music_archive_tick();assert(music_archive_wav&&music_archive_ready&&posts==2);
 int before=opens;music_archive_tick();assert(opens==before);clock_ms+=30000;
 /* POST committed but response was lost: list match must avoid a duplicate. */
 status=200;snprintf(reply,sizeof(reply),"{\"assets\":[{\"asset_id\":\"already_saved\",\"name\":\"%s\"}]}",music_archive_name);
 music_archive_tick();assert(!music_archive_wav&&posts==2&&get_count==1);
 fill_song();write_fail=true;music_archive_tick();assert(music_archive_ready&&music_archive_wav);write_fail=false;clock_ms+=30000;
 strcpy(reply,"{\"assets\":[]}");status=403;music_archive_tick();assert(music_archive_wav&&posts==3);clock_ms+=30000;
 /* Successful lookup without match, then successful upload. */
 strcpy(reply,"{\"assets\":[],\"asset_id\":\"saved_after_retry\"}");status=200;music_archive_tick();assert(!music_archive_wav&&posts==4);
 fill_song();g_wifi_ready=false;before=opens;music_archive_tick();assert(opens==before&&music_archive_wav);
 g_wifi_ready=true;power=true;music_archive_tick();assert(opens==before);power=false;
 strcpy(reply,"{\"asset_id\":\"back_online\"}");music_archive_tick();assert(!music_archive_wav);
 puts("PASS: real Assets WAV upload, short writes, pending retry, ambiguous POST deduplication, Wi-Fi/power gates, allocation refusal");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(s)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(root/'main'),'-I'+str(idf/'components/json/cJSON'),str(p/'test.c'),str(idf/'components/json/cJSON/cJSON.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
