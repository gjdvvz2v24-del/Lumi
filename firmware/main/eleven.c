/* Historical filename/API retained for the S3R UI. Conversation is OpenAI only. */
#include "bober.h"
#include "openai_profile.h"
#include "prompt_store.h"
#include "openai_protocol.h"
#include "session_control.h"
#include "tts_delivery.h"
#include "playback_gate.h"
#include "tts_limits.h"
#include "music_protocol.h"
#include "esp_random.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"
#include "esp_netif_sntp.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
static const char *TAG="openai";
static esp_websocket_client_handle_t ws;
static SemaphoreHandle_t ws_mutex;
static volatile bool transport,configured;
static atomic_bool wake_wait_requested,end_after_reply;
static uint32_t ws_generation;
static QueueHandle_t replies;
static char message[32768];
static size_t message_len;
static bool assembling;
typedef struct {uint32_t generation;char text[4096];} reply_t;
static bool current(uint32_t gen){return g_session_enabled&&g_wifi_ready&&!bober_power_requested()&&gen==g_generation;}
static void fail(void){g_ws_ready=false;g_ws_reconnect=true;}
static bool send_text(const char *text,size_t n){
 if(!ws_mutex||xSemaphoreTake(ws_mutex,pdMS_TO_TICKS(250))!=pdTRUE)return false;
 bool ok=ws&&transport&&current(ws_generation)&&esp_websocket_client_send_text(ws,text,n,pdMS_TO_TICKS(800))==(int)n;
 xSemaphoreGive(ws_mutex);return ok;
}
static const char *str(cJSON *j,const char *key){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)?v->valuestring:"";}
/* Close the current transport before publishing WAKE_WAIT. This prevents a
   second detection from racing old socket/PCM cleanup or reconnecting itself. */
static void request_wake_wait(void){
 atomic_store(&wake_wait_requested,true);
 g_session_enabled=false;g_ws_ready=false;g_phase=BOBER_OFF;
}
static void finish_wake_wait(void){
 if(atomic_exchange(&wake_wait_requested,false)&&!g_session_enabled&&!bober_power_requested()){
  g_ws_reconnect=false;g_phase=BOBER_WAKE_WAIT;
  ESP_LOGI(TAG,"Session ended; waiting locally for Hej Lumi, Luuu-miiii or strong shake");
 }
}
#include "mic_ready.inc"
static void reply_playback_finished(void){
 g_response_complete=false;
 if(atomic_exchange(&end_after_reply,false)){mic_ready_cue_active=false;request_wake_wait();}
 else if(mic_ready_cue_active){mic_ready_cue_active=false;g_phase=BOBER_LISTENING;ESP_LOGI(TAG,"Microphone ready: listening");}
 else begin_listening();
}
static void queue_reply(const char *text){
 if(!text[0]){g_response_complete=true;return;}
 if(strlen(text)>=sizeof(((reply_t*)0)->text)){fail();return;}
 reply_t *r=malloc(sizeof(*r));if(!r){fail();return;}
 r->generation=ws_generation;strlcpy(r->text,text,sizeof(r->text));
 if(xQueueSend(replies,r,0)!=pdTRUE)fail();
 free(r);
}
#ifdef LUMI_SOL_ULTRAFAST
#include "sol_ultrafast.inc"
#endif
/* Explicit diagnostic build only: conversation text is visible on local USB. */
static void trace_event(cJSON *root){
#ifdef BOBER_OPENAI_DIAGNOSTICS
 const char *type=str(root,"type");
 bool transcript=!strcmp(type,"conversation.item.input_audio_transcription.completed");
 bool response=!strcmp(type,"response.done");
 if(!transcript&&!response&&strcmp(type,"response.created")&&
    strcmp(type,"input_audio_buffer.committed")&&strcmp(type,"input_audio_buffer.speech_started")&&
    strcmp(type,"input_audio_buffer.speech_stopped"))return;
 cJSON *trace=cJSON_CreateObject();if(!trace)return;
 cJSON_AddStringToObject(trace,"event",type);
 cJSON_AddStringToObject(trace,"item_id",str(root,"item_id"));
 cJSON_AddNumberToObject(trace,"generation",ws_generation);
 cJSON_AddNumberToObject(trace,"phase",g_phase);
 if(transcript)cJSON_AddStringToObject(trace,"transcript",str(root,"transcript"));
 cJSON *reply=cJSON_GetObjectItem(root,"response");
 if(reply){
  cJSON_AddStringToObject(trace,"response_id",str(reply,"id"));
  cJSON_AddStringToObject(trace,"status",str(reply,"status"));
  if(response){
   cJSON *details=cJSON_GetObjectItem(reply,"status_details");
   cJSON *usage=cJSON_GetObjectItem(reply,"usage");
   if(details)cJSON_AddItemToObject(trace,"status_details",cJSON_Duplicate(details,true));
   if(usage)cJSON_AddItemToObject(trace,"usage",cJSON_Duplicate(usage,true));
   cJSON *output=cJSON_GetObjectItem(reply,"output");
   if(output)cJSON_AddItemToObject(trace,"output",cJSON_Duplicate(output,true));
  }
 }
 /* Escaping keeps transcript newlines/control characters within one JSON record. */
 char *line=cJSON_PrintUnformatted(trace);
 if(line){ESP_LOGI(TAG,"VOICE_TRACE %s",line);free(line);}
 cJSON_Delete(trace);
#else
 (void)root;
#endif
}
static void handle_message(void){
 cJSON *root=cJSON_Parse(message);if(!root){fail();return;}
 trace_event(root);
 const char *type=str(root,"type");
 if(atomic_load(&end_after_reply)){cJSON_Delete(root);return;}
 if(!strcmp(type,"conversation.item.input_audio_transcription.completed")){
  if(lumi_is_goodbye(str(root,"transcript")))request_wake_wait();
#ifdef LUMI_SOL_ULTRAFAST
  else sol_submit(str(root,"transcript"));
#endif
 }else if(!strcmp(type,"session.updated")){
  cJSON *session=cJSON_GetObjectItem(root,"session");
  cJSON *modalities=cJSON_GetObjectItem(session,"output_modalities");
  cJSON *mode=cJSON_GetArrayItem(modalities,0);
  cJSON *tools=cJSON_GetObjectItem(session,"tools");
  cJSON *input=cJSON_GetObjectItem(cJSON_GetObjectItem(session,"audio"),"input");
  cJSON *format=cJSON_GetObjectItem(input,"format");
  cJSON *rate=cJSON_GetObjectItem(format,"rate");
  cJSON *asr=cJSON_GetObjectItem(input,"transcription");
  configured=cJSON_IsString(mode)&&!strcmp(mode->valuestring,"text")&&cJSON_GetArraySize(modalities)==1&&
    cJSON_IsArray(tools)&&cJSON_GetArraySize(tools)==1&&
    !strcmp(str(cJSON_GetArrayItem(tools,0),"type"),"function")&&
    !strcmp(str(cJSON_GetArrayItem(tools,0),"name"),"end_conversation")&&!strcmp(str(format,"type"),"audio/pcm")&&
    cJSON_IsNumber(rate)&&rate->valueint==24000&&!strcmp(str(asr,"language"),"pl");
  if(!configured)fail();
#ifdef LUMI_SOL_ULTRAFAST
  else if(!cJSON_IsFalse(cJSON_GetObjectItem(cJSON_GetObjectItem(input,"turn_detection"),"create_response")))fail();
#endif
  else {g_ws_ready=true;g_phase=BOBER_CONNECTING;ESP_LOGI(TAG,"OpenAI ready: text output, Polish transcription, local end_conversation");}
 }else if(!strcmp(type,"response.created")){
  g_phase=BOBER_SPEAKING;g_response_complete=false;g_last_tts_begin_ms=bober_now_ms();bober_pcm_reset();
 }else if(!strcmp(type,"response.done")){
  cJSON *response=cJSON_GetObjectItem(root,"response");
  if(!strcmp(str(response,"status"),"incomplete")){
   const char *reason=str(cJSON_GetObjectItem(response,"status_details"),"reason");
   ESP_LOGW(TAG,"OpenAI incomplete: reason=%s; preserving session",reason[0]?reason:"unknown");
   /* Do not play partial output or retry a filtered response. Give a local
      notice through TTS, then resume the same session and conversation. */
   g_phase=BOBER_SPEAKING;g_response_complete=false;g_last_tts_begin_ms=bober_now_ms();
   if(!strcmp(reason,"content_filter"))queue_reply("Tego tekstu nie mogę dokończyć. Możemy zmienić temat.");
   else if(!strcmp(reason,"max_output_tokens"))queue_reply("Skończył mi się limit tej odpowiedzi. Powiedz, czy mam spróbować krócej.");
   else queue_reply("Nie udało mi się dokończyć odpowiedzi. Powiedz jeszcze raz.");
  }else if(strcmp(str(response,"status"),"completed")){ESP_LOGW(TAG,"OpenAI response not completed");fail();}
  else {
   char *text=calloc(4096,1);size_t used=0;bool ok=text!=NULL;
   cJSON *item,*part;bool ending=false;char farewell[513]={0};
   cJSON_ArrayForEach(item,cJSON_GetObjectItem(response,"output")){
    if(!strcmp(str(item,"type"),"function_call")){
     if(ending||strcmp(str(item,"name"),"end_conversation")){ok=false;break;}
     cJSON *args=cJSON_Parse(str(item,"arguments"));
     const char *bye=str(args,"farewell");size_t len=strlen(bye);
     bool valid=cJSON_IsObject(args)&&len>0&&len<sizeof(farewell)&&strspn(bye," \t\r\n")<len;
     if(valid){memcpy(farewell,bye,len+1);ending=true;}else ok=false;
     cJSON_Delete(args);if(!ok)break;continue;
    }
    if(strcmp(str(item,"type"),"message")){ok=false;break;}
    cJSON_ArrayForEach(part,cJSON_GetObjectItem(item,"content")){
     if(strcmp(str(part,"type"),"output_text")){ok=false;break;}
     const char *s=str(part,"text");size_t n=strlen(s);
     if(!ok||used+n+2>=4096){ok=false;break;}
     if(used)text[used++]=' ';
     memcpy(text+used,s,n+1);used+=n;
    }
   }
   if(ok){
    if(ending){
     atomic_store(&end_after_reply,true);
     g_phase=BOBER_SPEAKING;g_response_complete=false;g_last_tts_begin_ms=bober_now_ms();
    }
    queue_reply(ending?farewell:text);
   }else fail();free(text);
  }
 }else if(!strcmp(type,"error")||!strcmp(type,"conversation.item.input_audio_transcription.failed")){
  ESP_LOGE(TAG,"OpenAI event error code=%s (details omitted)",str(cJSON_GetObjectItem(root,"error"),"code"));fail();
 }
 cJSON_Delete(root);
}
static void ws_event(void *arg,esp_event_base_t base,int32_t event,void *data){
 (void)arg;(void)base;if(!current(ws_generation))return;
 esp_websocket_event_data_t *ev=data;
 if(event==WEBSOCKET_EVENT_CONNECTED){transport=true;}
 else if(event==WEBSOCKET_EVENT_DISCONNECTED||event==WEBSOCKET_EVENT_CLOSED||event==WEBSOCKET_EVENT_ERROR){
  ESP_LOGW(TAG,"ASR transport event=%ld generation=%lu phase=%d",(long)event,(unsigned long)ws_generation,g_phase);
  transport=false;fail();
 }
 else if(event==WEBSOCKET_EVENT_DATA&&ev){
  if(ev->op_code==0x9||ev->op_code==0xa)return;
  if(ev->op_code==0x8){transport=false;fail();return;}
  if(ev->op_code!=0x1&&ev->op_code!=0x0){fail();return;}
  if(ev->payload_offset==0&&ev->op_code==0x1){if(assembling){fail();return;}message_len=0;assembling=true;}
  if(!assembling||ev->data_len<0||(size_t)ev->data_len>=sizeof(message)-message_len){fail();return;}
  memcpy(message+message_len,ev->data_ptr,ev->data_len);message_len+=ev->data_len;
  if(ev->fin&&ev->payload_offset+ev->data_len==ev->payload_len){message[message_len]=0;assembling=false;handle_message();}
 }
}
static esp_err_t header(esp_http_client_event_t *ev){
 if(ev->event_id==HTTP_EVENT_ON_HEADER&&ev->header_key&&ev->header_value&&!strcasecmp(ev->header_key,"Content-Type")){
  const char *s=ev->header_value;
  if(strstr(s,"json")||strstr(s,"text/")||strstr(s,"wav")||strstr(s,"mpeg"))*(bool*)ev->user_data=true;
 }return ESP_OK;
}
/* Network owner performs TTS outside WebSocket callbacks. SDK keeps receiving
   WebSocket frames while this task streams HTTP into the existing PCM ring. */
static bool speak(const reply_t *r){
 int64_t tts_start=bober_now_ms();
 ESP_LOGI(TAG,"TTS TLS heap: internal_free=%u internal_largest=%u psram_free=%u",
  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 bool ok=false,bad=false;size_t received=0,carry=0;const char *stage="open";int http_status=0,io_error=0;
 int64_t deadline=tts_start+LUMI_TTS_TOTAL_MS,last_progress=tts_start,last_play=g_last_play_ms;
 char url[256];snprintf(url,sizeof(url),"https://api.elevenlabs.io/v1/text-to-speech/%s/stream?output_format=pcm_16000",g_cfg.voice_id);
 cJSON *j=lumi_tts_payload(r->text);if(!j)return false;
 ESP_LOGI(TAG,"TTS delivery=%s model=%s",lumi_tts_is_singing(r->text)?"singing":"speech",str(j,"model_id"));
 char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!body)return false;
 esp_http_client_config_t cfg={.url=url,.method=HTTP_METHOD_POST,.timeout_ms=LUMI_TTS_IDLE_MS,.buffer_size=2048,
  .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.event_handler=header,.user_data=&bad};
 esp_http_client_handle_t h=esp_http_client_init(&cfg);if(!h){free(body);return false;}
 uint8_t pcm[2048];
 esp_http_client_set_header(h,"xi-api-key",g_cfg.api_key);esp_http_client_set_header(h,"Content-Type","application/json");
 esp_http_client_set_header(h,"Accept","audio/pcm");
 if(!current(r->generation))goto done;
 io_error=esp_http_client_open(h,strlen(body));if(io_error!=ESP_OK)goto done;
 stage="write";
 for(size_t sent=0;sent<strlen(body);){
  if(!current(r->generation)||bober_now_ms()>deadline)goto done;
  int n=esp_http_client_write(h,body+sent,strlen(body)-sent);if(n<=0){io_error=n;goto done;}sent+=(size_t)n;
 }
 stage="headers";
 int64_t headers=esp_http_client_fetch_headers(h);if(headers<0){io_error=(int)headers;goto done;}
 http_status=esp_http_client_get_status_code(h);
 if(http_status!=200){stage="http_status";goto done;}
 if(bad){stage="content_type";goto done;}
 stage="read";
 while(current(r->generation)&&!g_ws_reconnect&&bober_now_ms()<deadline){
  if(bober_now_ms()-last_progress>LUMI_TTS_IDLE_MS){stage="no_progress";goto done;}
  int n=esp_http_client_read(h,(char*)pcm+carry,sizeof(pcm)-carry);
  if(n<0){io_error=n;goto done;}
  if(!n){if(esp_http_client_is_complete_data_received(h)){ok=received>0&&!carry;stage=carry?"incomplete_pcm":"empty_audio";break;}vTaskDelay(pdMS_TO_TICKS(10));continue;}
  last_progress=bober_now_ms();received+=(size_t)n;
  if(received>LUMI_TTS_MAX_AUDIO_BYTES){stage="audio_limit_5min";goto done;}
  if(received==(size_t)n)ESP_LOGI(TAG,"TTS first PCM: tts_ms=%lld turn_ms=%lld",(long long)(bober_now_ms()-tts_start),(long long)(bober_now_ms()-g_last_tts_begin_ms));
  size_t total=carry+(size_t)n,even=total&~(size_t)1;
  while(even&&!bober_pcm_push(pcm,even,r->generation)){
   if(g_last_play_ms!=last_play){last_play=g_last_play_ms;last_progress=bober_now_ms();}
   if(!current(r->generation)||g_ws_reconnect||bober_now_ms()>deadline||bober_now_ms()-last_progress>LUMI_TTS_IDLE_MS){stage="backpressure";goto done;}
   vTaskDelay(pdMS_TO_TICKS(10));
  }
  g_last_pcm_ms=bober_now_ms();carry=total-even;if(carry)pcm[0]=pcm[even];
 }
 done:
 if(!current(r->generation))stage="cancelled";
 else if(g_ws_reconnect)stage="ASR_reconnect";
 else if(bober_now_ms()>=deadline)stage="total_timeout";
 if(ok)ESP_LOGI(TAG,"TTS complete: audio_ms=%u elapsed_ms=%lld",(unsigned)((uint64_t)received*1000u/(16000u*2u)),(long long)(bober_now_ms()-tts_start));
 else ESP_LOGW(TAG,"TTS interrupted: stage=%s status=%d io_error=%d audio_ms=%u elapsed_ms=%lld reconnect=%d",stage,http_status,io_error,(unsigned)((uint64_t)received*1000u/(16000u*2u)),(long long)(bober_now_ms()-tts_start),g_ws_reconnect);
 free(body);esp_http_client_close(h);esp_http_client_cleanup(h);
 if(ok&&current(r->generation))g_response_complete=true;
 return ok;
}
#ifdef LUMI_SOL_ULTRAFAST
#include "sol_fillers.inc"
#include "music_archive.inc"
#include "music_stream.inc"
#endif
static bool wait_clock(void){
 for(unsigned i=0;i<26&&g_wifi_ready&&!bober_power_requested();i++){
  if(time(NULL)>1767225600)return true;
  esp_netif_sntp_sync_wait(pdMS_TO_TICKS(500));
 }return false;
}
void bober_eleven_task(void *arg){
 (void)arg;ws_mutex=xSemaphoreCreateMutex();replies=xQueueCreate(1,sizeof(reply_t));
#ifdef LUMI_SOL_ULTRAFAST
 sol_requests=xQueueCreate(1,sizeof(reply_t));
 if(!sol_requests){vTaskDelete(NULL);return;}
 ESP_LOGW(TAG,"EXPERIMENT: Sol Ultrafast responses; Realtime only ASR/VAD");
#endif
 if(!ws_mutex||!replies){ESP_LOGE(TAG,"allocation failed");vTaskDelete(NULL);return;}
 esp_sntp_config_t sync=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");esp_netif_sntp_init(&sync);
 reply_t *r=malloc(sizeof(*r));if(!r){vTaskDelete(NULL);return;}
 for(;;){
  bober_power_checkpoint(BOBER_POWER_NETWORK);
  if(!g_wifi_ready){vTaskDelay(pdMS_TO_TICKS(150));continue;}
#ifdef LUMI_SOL_ULTRAFAST
  if(!g_session_enabled)music_archive_tick();
#endif
  if(!g_session_enabled){vTaskDelay(pdMS_TO_TICKS(150));continue;}
  uint32_t gen=g_generation;
  if(!g_cfg.openai_key[0]||!g_cfg.api_key[0]){ESP_LOGE(TAG,"Missing OpenAI/TTS key; configure_usb.py required");g_session_enabled=false;g_phase=BOBER_OFF;continue;}
  if(!wait_clock()||!current(gen)){vTaskDelay(pdMS_TO_TICKS(1000));continue;}
  char headers[560];snprintf(headers,sizeof(headers),"Authorization: Bearer %s\r\n",g_cfg.openai_key);
  ws_generation=gen;transport=false;configured=false;g_ws_ready=false;g_ws_reconnect=false;
  g_phase=BOBER_CONNECTING;g_response_complete=false;mic_ready_cue_active=false;assembling=false;message_len=0;
  xQueueReset(replies);bober_pcm_reset();if(g_uplink)xStreamBufferReset(g_uplink);
#ifdef LUMI_SOL_ULTRAFAST
  xQueueReset(sol_requests);sol_begin_generation(gen);
#endif
  esp_websocket_client_config_t cfg={.uri="wss://api.openai.com/v1/realtime?model=" BOBER_OPENAI_MODEL,
   .headers=headers,.crt_bundle_attach=esp_crt_bundle_attach,.buffer_size=4096,.task_stack=12288,
   .network_timeout_ms=10000,.disable_auto_reconnect=true,.ping_interval_sec=18};
  ws=esp_websocket_client_init(&cfg);
  if(!ws){vTaskDelay(pdMS_TO_TICKS(2000));continue;}
  esp_websocket_register_events(ws,WEBSOCKET_EVENT_ANY,ws_event,NULL);
  esp_err_t e=esp_websocket_client_start(ws);
  int64_t start=bober_now_ms();bool hello=false,greeted=false;
#ifdef LUMI_SOL_ULTRAFAST
  greeted=sol_greeted;
#endif
  while(e==ESP_OK&&current(gen)&&!g_ws_reconnect){
   int64_t now=bober_now_ms();
   if(!hello&&transport){
    char *session_json=bober_prompt_session(bober_session_update);
#ifdef LUMI_SOL_ULTRAFAST
    session_json=sol_prepare(session_json);
#endif
    if(!session_json){ESP_LOGE(TAG,"Cannot load prompt configuration");break;}
    hello=send_text(session_json,strlen(session_json));free(session_json);
   }
   if(!configured&&now-start>25000)break;
   if(configured&&!transport)break;
#ifdef LUMI_SOL_ULTRAFAST
   if(configured&&g_phase==BOBER_SPEAKING)music_archive_tick();
#endif
   if(configured&&greeted&&g_phase==BOBER_CONNECTING)begin_listening();
   if(configured&&!greeted){
    greeted=true;g_phase=BOBER_SPEAKING;g_response_complete=false;g_last_tts_begin_ms=now;
    r->generation=gen;strlcpy(r->text,bober_greeting,sizeof(r->text));if(!speak(r))break;
#ifdef LUMI_SOL_ULTRAFAST
    sol_greeted=true;
#endif
   }
   if(xQueueReceive(replies,r,0)==pdTRUE&&r->generation==gen){if(!speak(r)){ESP_LOGW(TAG,"Reconnect: queued TTS failed");break;}}
#ifdef LUMI_SOL_ULTRAFAST
   if(xQueueReceive(sol_requests,r,0)==pdTRUE&&current(r->generation)){
    if(!sol_generate_with_filler(r)){
     if(!current(gen))break;
     ESP_LOGE(TAG,"Sol request failed; no automatic retry or backend fallback");
     strlcpy(r->text,"Nie udało mi się pobrać odpowiedzi. Spróbuj jeszcze raz.",sizeof(r->text));
    }
    if(sol_music_requested){
     sol_music_succeeded=play_random_song(r);
     if(!current(gen)||g_ws_reconnect)break;
     if(sol_music_succeeded)continue;
     bober_pcm_reset();
     strlcpy(r->text,music_archive_blocked?"Jeszcze zapisuję poprzednią piosenkę. Spróbuj za chwilę.":"Nie udało mi się wygenerować piosenki. Spróbuj później.",sizeof(r->text));
    }
    if(!current(gen)||!speak(r)){ESP_LOGW(TAG,"Reconnect: Sol TTS failed or session cancelled");break;}
   }
#endif
   if(configured&&g_phase==BOBER_SPEAKING){
#ifdef LUMI_SOL_ULTRAFAST
    bool drained=lumi_sol_mic_can_resume(g_response_complete,bober_pcm_available(),lumi_reaction_busy(),now,g_last_play_ms);
#else
    bool drained=!lumi_reaction_busy()&&g_response_complete&&bober_pcm_available()==0&&now-g_last_play_ms>200&&now-g_last_pcm_ms>350;
#endif
    if(drained){
     ESP_LOGI(TAG,"Playback drained; microphone resumes after tail_ms=%lld",(long long)(now-g_last_play_ms));
     reply_playback_finished();if(g_uplink)xStreamBufferReset(g_uplink);
    }else if(lumi_tts_playback_stalled(now,g_last_tts_begin_ms,g_last_pcm_ms,g_last_play_ms)){
     ESP_LOGW(TAG,"Playback stalled: no PCM/playback progress for 90 s");break;
    }
   }
#ifdef LUMI_SOL_ULTRAFAST
   if(sol_idle_expired(now)){ESP_LOGI(TAG,"Session ended after 10 minutes idle");request_wake_wait();break;}
#else
   if(now-start>600000){request_wake_wait();break;}
#endif
   vTaskDelay(pdMS_TO_TICKS(20));
  }
  if(atomic_exchange(&end_after_reply,false))request_wake_wait();
  g_ws_ready=false;transport=false;
  xSemaphoreTake(ws_mutex,portMAX_DELAY);
  if(e==ESP_OK)esp_websocket_client_stop(ws);
  esp_websocket_client_destroy(ws);ws=NULL;memset(headers,0,sizeof(headers));
  xSemaphoreGive(ws_mutex);bober_pcm_reset();g_response_complete=false;
  xQueueReset(replies);if(g_uplink)xStreamBufferReset(g_uplink);
  finish_wake_wait();
#ifdef LUMI_SOL_ULTRAFAST
  if(!current(gen))sol_http_drop();
#endif
  if(current(gen)){g_phase=BOBER_CONNECTING;for(int i=0;i<30&&current(gen);i++)vTaskDelay(pdMS_TO_TICKS(100));}
 }
}
void bober_uplink_task(void *arg){
 (void)arg;int16_t pcm[320],converted[480];char msg[1400];bober_resampler_t resampler={0};int failures=0;
 for(;;){
  bober_power_checkpoint(BOBER_POWER_UPLINK);
  if(!g_session_enabled||!g_ws_ready||g_phase!=BOBER_LISTENING){memset(&resampler,0,sizeof(resampler));failures=0;vTaskDelay(pdMS_TO_TICKS(25));continue;}
  uint32_t gen=g_generation;
  size_t n=xStreamBufferReceive(g_uplink,pcm,sizeof(pcm),pdMS_TO_TICKS(90));
  if(!current(gen)||!g_ws_ready||g_phase!=BOBER_LISTENING)continue;
  n&=~(size_t)1;if(!n)continue;
  size_t count=bober_resample(&resampler,pcm,n/2,converted),encoded=0;
  static const char prefix[]="{\"type\":\"input_audio_buffer.append\",\"audio\":\"";
  memcpy(msg,prefix,sizeof(prefix)-1);
  if(mbedtls_base64_encode((unsigned char*)msg+sizeof(prefix)-1,sizeof(msg)-sizeof(prefix)-4,&encoded,(uint8_t*)converted,count*2)!=0){fail();continue;}
  size_t len=sizeof(prefix)-1+encoded;msg[len++]='"';msg[len++]='}';msg[len]=0;
  if(!send_text(msg,len)){if(++failures>=5)fail();}else failures=0;
 }
}
