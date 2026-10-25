#!/usr/bin/env python3
"""Execute the experimental C backend with fake HTTP; no paid API calls."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
IDF = Path(os.environ.get("IDF_PATH", "/Users/grpi/Work/lumi/esp-idf"))
HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "cJSON.h"
#include "openai_profile.h"
typedef struct {uint32_t generation;char text[4096];} reply_t;
typedef int QueueHandle_t;
typedef int esp_http_client_handle_t;
typedef struct {const char *url;int method,timeout_ms,buffer_size;void *crt_bundle_attach;bool disable_auto_redirect;} esp_http_client_config_t;
#define ESP_OK 0
#define HTTP_METHOD_POST 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdTRUE 1
#define BOBER_LISTENING 3
#define BOBER_SPEAKING 2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
static void *esp_crt_bundle_attach;
static struct {char openai_key[512];} g_cfg={"test-key"};
static bool active=true,g_response_complete,failed;
static int g_phase=BOBER_LISTENING;
static int64_t g_last_tts_begin_ms;
static uint32_t ws_generation=7;
static atomic_bool end_after_reply;
static reply_t queued;
static const char *fake_response;
static int fake_status=200;
static size_t read_pos;
static bool cancel_on_read;
static char request_body[32768];
static const char *str(cJSON *j,const char *key){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)?v->valuestring:"";}
static bool current(uint32_t gen){return active&&gen==7;}
static void fail(void){failed=true;}
static void bober_pcm_reset(void){}
static int64_t bober_now_ms(void){return 100;}
static void vTaskDelay(int n){(void)n;}
static int xQueueSend(int q,reply_t *r,int wait){(void)q;(void)wait;queued=*r;return 1;}
static void *heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}
static int esp_http_client_init(esp_http_client_config_t *c){(void)c;read_pos=0;return 1;}
static void esp_http_client_set_header(int h,const char *k,const char *v){(void)h;(void)k;(void)v;}
static int esp_http_client_open(int h,size_t n){(void)h;(void)n;return 0;}
static int esp_http_client_write(int h,const char *s,size_t n){(void)h;assert(n<sizeof(request_body));memcpy(request_body,s,n);request_body[n]=0;return (int)n;}
static int esp_http_client_fetch_headers(int h){(void)h;return 0;}
static int esp_http_client_get_status_code(int h){(void)h;return fake_status;}
static int esp_http_client_read(int h,char *s,size_t n){(void)h;if(cancel_on_read)active=false;size_t left=strlen(fake_response)-read_pos;if(n>left)n=left;memcpy(s,fake_response+read_pos,n);read_pos+=n;return (int)n;}
static bool esp_http_client_is_complete_data_received(int h){(void)h;return read_pos==strlen(fake_response);}
static void esp_http_client_close(int h){(void)h;}
static void esp_http_client_cleanup(int h){(void)h;}
#include "sol_ultrafast.inc"
int main(void){
 char *update=sol_prepare(strdup(bober_session_update));assert(update);
 cJSON *j=cJSON_Parse(update);free(update);
 cJSON *session=cJSON_GetObjectItem(j,"session");
 cJSON *vad=cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(session,"audio"),"input"),"turn_detection");
 assert(cJSON_IsFalse(cJSON_GetObjectItem(vad,"create_response")));
 assert(!strcmp(str(sol_template,"model"),"gpt-6.1-sol"));
 assert(!strcmp(str(sol_template,"service_tier"),"ultrafast"));
 assert(!strcmp(str(sol_template,"instructions"),str(session,"instructions")));cJSON_Delete(j);
 sol_submit("Cześć");assert(g_phase==BOBER_SPEAKING&&queued.generation==7&&!g_response_complete&&!failed);
 sol_submit("Ignored overlapping turn");assert(!strcmp(queued.text,"Cześć"));
 fake_response="{\"id\":\"resp_ok\",\"status\":\"completed\",\"service_tier\":\"ultrafast\",\"output\":[{\"type\":\"reasoning\"},{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"Hej!\"}]}]}";
 assert(sol_generate(&queued));assert(!strcmp(queued.text,"Hej!")&&!strcmp(sol_previous,"resp_ok"));
 assert(strstr(request_body,"Cześć")&&!strstr(request_body,"previous_response_id"));
 assert(sol_generate(&queued));assert(strstr(request_body,"previous_response_id"));
 fake_response="{\"id\":\"wrong\",\"status\":\"incomplete\",\"service_tier\":\"ultrafast\",\"output\":[]}";
 assert(!sol_generate(&queued));assert(!strcmp(sol_previous,"resp_ok"));
 fake_response="{\"id\":\"wrong\",\"status\":\"completed\",\"service_tier\":\"default\",\"output\":[]}";
 assert(!sol_generate(&queued));
 fake_response="{\"id\":\"bye\",\"status\":\"completed\",\"service_tier\":\"ultrafast\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"Duplicate\"}]},{\"type\":\"function_call\",\"name\":\"end_conversation\",\"arguments\":\"{\\\"farewell\\\":\\\"Pa!\\\"}\"}]}";
 assert(sol_generate(&queued));assert(!strcmp(queued.text,"Pa!")&&atomic_load(&end_after_reply));
 cancel_on_read=true;assert(!sol_generate(&queued));assert(!strcmp(sol_previous,"bye"));
 active=true;cancel_on_read=false;fake_status=403;assert(!sol_generate(&queued));
 cJSON_Delete(sol_template);puts("PASS: actual C backend config, queue gate, text, history, incomplete/tier/error rejection, farewell, cancellation");
}
'''

with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "test.c"
    source.write_text(HARNESS)
    binary = Path(directory) / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-deprecated-declarations", "-fsanitize=address,undefined",
                    "-I" + str(ROOT / "main"), "-I" + str(IDF / "components/json/cJSON"),
                    str(source), str(IDF / "components/json/cJSON/cJSON.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
