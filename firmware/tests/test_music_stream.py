#!/usr/bin/env python3
"""Run real Music producer: stereo frames, 60 s, partial chunks, failures, history-independent cancellation."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
idf=Path(os.environ.get('IDF_PATH','/Users/grpi/Work/lumi/esp-idf'))
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "music_protocol.h"
#define ESP_OK 0
#define HTTP_METHOD_POST 1
#define pdMS_TO_TICKS(x) (x)
static char last_log[512];static const char *TAG="test";
#define ESP_LOGI(tag,fmt,...) ((void)(tag),(void)snprintf(last_log,sizeof(last_log),fmt,##__VA_ARGS__))
#define ESP_LOGW(tag,fmt,...) ESP_LOGI(tag,fmt,##__VA_ARGS__)
typedef struct {uint32_t generation;char text[4096];} reply_t;
typedef int esp_http_client_handle_t;
typedef struct {const char *url;int method,timeout_ms,buffer_size;void *crt_bundle_attach;bool disable_auto_redirect;void *event_handler,*user_data;} esp_http_client_config_t;
static void *esp_crt_bundle_attach,*header;
static struct {char api_key[32];} g_cfg={"fake-key"};
static bool active,g_ws_reconnect,g_response_complete,full_fifo,stalled;
static int64_t clock_ms,g_last_play_ms,g_last_pcm_ms,sol_last_activity_ms;
static size_t input_size,read_bytes,output_size,chunk;
static int opens,closes,status,cancel_at,error_at,reads,header_delay;
static uint8_t output[LUMI_MUSIC_BYTES+320],input[LUMI_MUSIC_BYTES*2+4];
static uint32_t esp_random(void){return 42;}
static int64_t bober_now_ms(void){return clock_ms;}
static bool current(uint32_t gen){return active&&gen==7;}
static void vTaskDelay(int ms){clock_ms+=ms;}
static bool bober_pcm_push(const uint8_t *p,size_t n,uint32_t gen){
 assert(gen==7&&p&&n&&!(n%2));
 if(full_fifo||!active)return false;
 assert(output_size+n<=sizeof(output));memcpy(output+output_size,p,n);output_size+=n;return true;
}
static int esp_http_client_init(esp_http_client_config_t *c){
 assert(c->timeout_ms==120000&&c->disable_auto_redirect);
 assert(!strcmp(c->url,"https://api.elevenlabs.io/v1/music?output_format=pcm_16000"));return 1;
}
static void esp_http_client_set_header(int h,const char *key,const char *value){assert(h&&key&&value);}
static int esp_http_client_open(int h,size_t n){assert(h&&n&&output_size==320&&!g_response_complete);opens++;return 0;}
static int esp_http_client_write(int h,const char *body,size_t n){
 assert(h&&n);cJSON *j=cJSON_ParseWithLength(body,n);assert(j);
 assert(!strcmp(cJSON_GetObjectItem(j,"model_id")->valuestring,"music_v2"));
 assert(cJSON_GetObjectItem(j,"music_length_ms")->valueint==60000);
 assert(cJSON_IsFalse(cJSON_GetObjectItem(j,"force_instrumental")));
 cJSON_Delete(j);return (int)n;
}
static int esp_http_client_fetch_headers(int h){assert(h);clock_ms+=header_delay;return 0;}
static int esp_http_client_get_status_code(int h){assert(h);return status;}
static int esp_http_client_read(int h,char *p,size_t cap){
 assert(h);reads++;if(reads%100==0)clock_ms++;
 if(cancel_at==reads)active=false;
 if(error_at==reads)return -1;
 if(stalled)return 0;
 size_t n=input_size-read_bytes;if(n>cap)n=cap;if(n>chunk)n=chunk;
 memcpy(p,input+read_bytes,n);read_bytes+=n;return (int)n;
}
static bool esp_http_client_is_complete_data_received(int h){assert(h);return read_bytes==input_size&&!stalled;}
static void esp_http_client_close(int h){assert(h);closes++;}
static void esp_http_client_cleanup(int h){assert(h);}
__asm__(".pushsection __TEXT,__const\n.globl _binary_music_wait_pcm_start\n_binary_music_wait_pcm_start:\n.space 320\n.globl _binary_music_wait_pcm_end\n_binary_music_wait_pcm_end:\n.popsection");
static uint8_t archived[LUMI_MUSIC_BYTES];static size_t archived_bytes;
static bool music_archive_ready,begin_ok=true;
static int archive_calls;
static bool music_archive_begin(void){archived_bytes=0;music_archive_ready=false;return begin_ok;}
static void music_archive_discard(void){archived_bytes=0;music_archive_ready=false;}
static bool music_archive_append(const uint8_t *p,size_t n){assert(archived_bytes+n<=sizeof(archived));memcpy(archived+archived_bytes,p,n);archived_bytes+=n;return true;}
static void music_archive_tick(void){assert(music_archive_ready&&archived_bytes==LUMI_MUSIC_BYTES);archive_calls++;}
#include "music_stream.inc"
static void reset(void){
 archive_calls=0;begin_ok=true;active=true;g_ws_reconnect=g_response_complete=full_fifo=stalled=false;
 clock_ms=1000;g_last_play_ms=g_last_pcm_ms=sol_last_activity_ms=0;
 input_size=sizeof(input)-4;read_bytes=output_size=0;chunk=2048;
 opens=closes=reads=cancel_at=error_at=header_delay=0;status=200;
 /* Distinct channels and sign changes catch byte-order errors, overflow,
    one-channel playback, and lost/duplicated partial stereo frames. */
 for(size_t i=0;i<input_size/4;i++){
  int16_t left=i%2?30000:-30000,right=i%2?10000:-10000;
  uint16_t l=(uint16_t)left,r=(uint16_t)right;
  input[4*i]=l;input[4*i+1]=l>>8;input[4*i+2]=r;input[4*i+3]=r>>8;
 }
}
int main(void){
 reply_t r={.generation=7,.text="{\"prompt\":\"Piosenka o Zosi i jej czerwonym rowerze\",\"style\":\"\"}"};
 for(unsigned pick=0;pick<64;pick++){
  cJSON *j=lumi_music_payload(r.text,pick);assert(j);
  const char *prompt=cJSON_GetObjectItem(j,"prompt")->valuestring;
  assert(strstr(prompt,"Polish")&&strstr(prompt,"vocals"));cJSON_Delete(j);
 }
 cJSON *tool=lumi_music_tool();assert(tool&&cJSON_IsTrue(cJSON_GetObjectItem(tool,"strict")));cJSON_Delete(tool);
 const size_t chunks[]={2048,2047,511,3};
 for(size_t c=0;c<sizeof(chunks)/sizeof(*chunks);c++){
  reset();chunk=chunks[c];assert(play_song(&r));
  assert(output_size==LUMI_MUSIC_BYTES+320&&g_response_complete&&opens==1&&closes==1);
  assert(archive_calls==1&&archived_bytes==LUMI_MUSIC_BYTES&&!memcmp(archived,output+320,LUMI_MUSIC_BYTES));
  for(size_t i=0;i<LUMI_MUSIC_BYTES/2;i++){
   int16_t got=(int16_t)((uint16_t)output[320+2*i]|((uint16_t)output[321+2*i]<<8));
   assert(got==(i%2?20000:-20000));
  }
 }
 reset();begin_ok=false;assert(!play_song(&r)&&!opens&&!output_size);
 reset();status=401;assert(!play_song(&r)&&opens==1&&output_size==320&&!g_response_complete);
 reset();status=403;assert(!play_song(&r)&&opens==1&&output_size==320);
 reset();input_size--;assert(!play_song(&r)&&!g_response_complete);
 reset();input_size+=4;assert(!play_song(&r)&&!g_response_complete);
 reset();cancel_at=2;assert(!play_song(&r)&&!g_response_complete);
 reset();error_at=2;assert(!play_song(&r)&&opens==1);
 reset();g_ws_reconnect=true;assert(!play_song(&r)&&!opens);
 reset();stalled=true;assert(!play_song(&r)&&clock_ms<32000);
 reset();full_fifo=true;assert(!play_song(&r)&&!opens&&clock_ms<12000);
 reset();header_delay=180001;assert(!play_song(&r)&&output_size==320);
 puts("PASS: real Music producer, 60 s stereo downmix, split frames, duration, one-shot HTTP errors, cancellation, stall/deadline");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(harness)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(root/'main'),'-I'+str(idf/'components/json/cJSON'),str(p/'test.c'),str(idf/'components/json/cJSON/cJSON.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
