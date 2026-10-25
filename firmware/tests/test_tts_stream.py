#!/usr/bin/env python3
"""Execute real TTS producer with >45 s PCM, bounded FIFO and simulated speaker/time."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
idf=Path(os.environ.get('IDF_PATH','/Users/grpi/Work/lumi/esp-idf'))
s=(root/'main/eleven.c').read_text()
body=s[s.index('static bool speak('):s.index('#ifdef LUMI_SOL_ULTRAFAST\n#include "sol_fillers.inc"')]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "tts_limits.h"
#include "playback_gate.h"
#define ESP_OK 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
#define HTTP_METHOD_POST 1
#define pdMS_TO_TICKS(x) (x)
static char last_log[512];static const char *TAG="test";
#define ESP_LOGI(tag,fmt,...) ((void)(tag),(void)snprintf(last_log,sizeof(last_log),fmt,##__VA_ARGS__))
#define ESP_LOGW(tag,fmt,...) ESP_LOGI(tag,fmt,##__VA_ARGS__)
typedef struct {uint32_t generation;char text[4096];} reply_t;
typedef int esp_http_client_handle_t;
typedef struct {const char *url;int method,timeout_ms,buffer_size;void *crt_bundle_attach;bool disable_auto_redirect;void *event_handler,*user_data;} esp_http_client_config_t;
static void *esp_crt_bundle_attach,*header;
static struct {char voice_id[96],api_key[192];} g_cfg={"test-voice","test-key"};
static bool active=true,g_ws_reconnect,g_response_complete,playback=true,incomplete_stream;
static int64_t clock_ms=1000,g_last_play_ms,g_last_pcm_ms,g_last_tts_begin_ms=1000;
static size_t audio_bytes,read_bytes,queued,pushed,max_queued;
static int opens,closes,read_calls,cancel_at,reconnect_at,read_error_at;
static int http_status=200;
#define FIFO_CAPACITY (2u*1024u*1024u)
static int64_t bober_now_ms(void){return clock_ms;}
static bool current(uint32_t generation){return active&&generation==7;}
static unsigned heap_caps_get_free_size(int caps){(void)caps;return 100000;}
static unsigned heap_caps_get_largest_free_block(int caps){(void)caps;return 50000;}
static const char *str(cJSON *j,const char *key){(void)j;(void)key;return "fake";}
static cJSON *lumi_tts_payload(const char *text){assert(text[0]);return cJSON_CreateObject();}
static bool lumi_tts_is_singing(const char *text){(void)text;return false;}
static void advance(int ms){
 clock_ms+=ms;
 if(playback&&queued){size_t n=(size_t)ms*32;if(n>queued)n=queued;queued-=n;if(n)g_last_play_ms=clock_ms;}
}
static void vTaskDelay(int ms){advance(ms);}
static bool bober_pcm_push(const uint8_t *p,size_t n,uint32_t generation){
 assert(p&&n&&!(n%2));if(!current(generation)||n>FIFO_CAPACITY-queued)return false;
 queued+=n;pushed+=n;if(queued>max_queued)max_queued=queued;return true;
}
static int esp_http_client_init(esp_http_client_config_t *cfg){assert(cfg->timeout_ms==30000);return 1;}
static void esp_http_client_set_header(int h,const char *key,const char *value){(void)h;(void)key;(void)value;}
static int esp_http_client_open(int h,size_t n){assert(h&&n);opens++;return 0;}
static int esp_http_client_write(int h,const char *body,size_t n){assert(h&&body&&n);return (int)n;}
static int esp_http_client_fetch_headers(int h){assert(h);return 0;}
static int esp_http_client_get_status_code(int h){assert(h);return http_status;}
static int esp_http_client_read(int h,char *pcm,size_t capacity){
 assert(h);read_calls++;advance(1);if(read_error_at&&read_calls==read_error_at)return -77;
 if(cancel_at&&read_calls==cancel_at)active=false;
 if(reconnect_at&&read_calls==reconnect_at)g_ws_reconnect=true;
 size_t n=audio_bytes-read_bytes;if(n>capacity)n=capacity;
 memset(pcm,1,n);read_bytes+=n;return (int)n;
}
static bool esp_http_client_is_complete_data_received(int h){assert(h);return read_bytes==audio_bytes&&!incomplete_stream;}
static void esp_http_client_close(int h){assert(h);closes++;}
static void esp_http_client_cleanup(int h){assert(h);}
'''
main=r'''
static void reset(size_t bytes){
 active=playback=true;g_ws_reconnect=g_response_complete=incomplete_stream=false;
 clock_ms=g_last_tts_begin_ms=1000;g_last_play_ms=g_last_pcm_ms=0;
 audio_bytes=bytes;read_bytes=queued=pushed=max_queued=0;
 opens=closes=read_calls=cancel_at=reconnect_at=read_error_at=0;http_status=200;
}
int main(void){
 reply_t r={.generation=7,.text="Long story"};
 reset(16000u*2u*180u);assert(speak(&r));
 assert(g_response_complete&&read_bytes==audio_bytes&&pushed==audio_bytes&&max_queued<=FIFO_CAPACITY&&opens==1&&closes==1);
 assert(clock_ms-g_last_tts_begin_ms>90000); /* Exceeds both former 60 s download and 90 s playback limits. */
 assert(strstr(last_log,"audio_ms=180000"));
 assert(!lumi_sol_mic_can_resume(g_response_complete,queued,false,clock_ms,g_last_play_ms));
 assert(!lumi_tts_playback_stalled(clock_ms,g_last_tts_begin_ms,g_last_pcm_ms,g_last_play_ms));
 advance((int)(queued/32)+1);advance(120);
 assert(lumi_sol_mic_can_resume(g_response_complete,queued,false,clock_ms,g_last_play_ms));
 assert(lumi_tts_playback_stalled(clock_ms+90001,clock_ms,clock_ms,clock_ms));
 assert(!lumi_tts_playback_stalled(clock_ms+90000,clock_ms,clock_ms,clock_ms));
 reset(LUMI_TTS_MAX_AUDIO_BYTES);assert(speak(&r)&&pushed==audio_bytes); /* Full 5 minutes, no unbounded buffer. */
 reset(LUMI_TTS_MAX_AUDIO_BYTES+2);assert(!speak(&r)&&!g_response_complete&&strstr(last_log,"audio_limit_5min"));
 reset(FIFO_CAPACITY+4096);playback=false;assert(!speak(&r)&&!g_response_complete&&strstr(last_log,"backpressure"));
 reset(0);incomplete_stream=true;assert(!speak(&r)&&!g_response_complete&&clock_ms>=31000);
 reset(1);assert(!speak(&r)&&!g_response_complete); /* Incomplete PCM sample. */
 reset(16000);cancel_at=3;assert(!speak(&r)&&!g_response_complete&&closes==1&&strstr(last_log,"cancelled"));
 reset(16000);reconnect_at=3;assert(!speak(&r)&&!g_response_complete&&closes==1&&strstr(last_log,"ASR_reconnect"));
 reset(16000);http_status=401;assert(!speak(&r)&&!g_response_complete&&!read_calls&&closes==1&&strstr(last_log,"status=401"));
 reset(16000);read_error_at=3;assert(!speak(&r)&&strstr(last_log,"io_error=-77")&&!g_response_complete);
 puts("PASS: production TTS streams full 3/5-minute audio with bounded FIFO, keeps mic gated, detects actual stalls/limits/errors and respects cancellation");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(prefix+body+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(root/'main'),'-I'+str(idf/'components/json/cJSON'),str(p/'test.c'),str(idf/'components/json/cJSON/cJSON.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
