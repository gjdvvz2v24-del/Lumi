/* Experimental backend, included only with -DLUMI_SOL_ULTRAFAST=ON.
   WebSocket callback queues transcripts; network owner performs HTTPS then TTS.
   No API keys or conversation text are logged by this backend. */
static QueueHandle_t sol_requests;
static cJSON *sol_template;
static char sol_previous[160];
static uint32_t sol_context_generation;
static bool sol_greeted;
static int64_t sol_last_activity_ms;
static esp_http_client_handle_t sol_http;
static int64_t sol_http_used_ms;
static void sol_http_drop(void){
 if(sol_http){esp_http_client_close(sol_http);esp_http_client_cleanup(sol_http);sol_http=0;}
}
static bool sol_idle_expired(int64_t now){return now>=sol_last_activity_ms&&now-sol_last_activity_ms>600000;}
/* ASR transport reconnect is not a new user conversation. */
static void sol_begin_generation(uint32_t generation){
 if(sol_context_generation==generation)return;
 sol_http_drop();sol_context_generation=generation;sol_previous[0]=0;sol_greeted=false;sol_last_activity_ms=bober_now_ms();
}

static char *sol_prepare(char *session_json){
 if(!session_json)return NULL;
 cJSON *root=cJSON_Parse(session_json);free(session_json);
 cJSON *session=cJSON_GetObjectItem(root,"session");
 cJSON *vad=cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(session,"audio"),"input"),"turn_detection");
 cJSON *tools=cJSON_GetObjectItem(session,"tools");
 if(!cJSON_IsObject(vad)||!cJSON_IsArray(tools)){cJSON_Delete(root);return NULL;}
 cJSON_ReplaceItemInObject(vad,"create_response",cJSON_CreateFalse());
 cJSON_ReplaceItemInObject(vad,"silence_duration_ms",cJSON_CreateNumber(650));
 cJSON_Delete(sol_template);sol_template=cJSON_CreateObject();
 cJSON *reason=cJSON_CreateObject();
 bool ok=sol_template&&reason&&
  cJSON_AddStringToObject(sol_template,"model","gpt-6.1-sol")&&
  cJSON_AddStringToObject(sol_template,"service_tier","ultrafast")&&
  cJSON_AddStringToObject(sol_template,"instructions",str(session,"instructions"))&&
  cJSON_AddNumberToObject(sol_template,"max_output_tokens",1024)&&
  cJSON_AddStringToObject(reason,"effort","low");
 if(ok){cJSON_AddItemToObject(sol_template,"reasoning",reason);reason=NULL;
  cJSON *copy=cJSON_Duplicate(tools,true);ok=copy&&cJSON_AddItemToObject(sol_template,"tools",copy);
  if(!ok)cJSON_Delete(copy);
 }
 cJSON_Delete(reason);
 char *out=ok?cJSON_PrintUnformatted(root):NULL;cJSON_Delete(root);return out;
}

static void sol_submit(const char *text){
 if(!text[0]||!current(ws_generation)||g_phase!=BOBER_LISTENING)return;
 if(strlen(text)>=sizeof(((reply_t*)0)->text)){fail();return;}
 reply_t *r=malloc(sizeof(*r));if(!r){fail();return;}
 r->generation=ws_generation;strlcpy(r->text,text,sizeof(r->text));
 sol_last_activity_ms=bober_now_ms();
 /* Gate uplink before queue publication, so the next turn cannot overlap. */
 g_phase=BOBER_SPEAKING;g_response_complete=false;g_last_tts_begin_ms=bober_now_ms();bober_pcm_reset();
 if(xQueueSend(sol_requests,r,0)!=pdTRUE)fail();
 free(r);
}

static bool sol_generate(reply_t *r){
 bool ok=false;char *body=NULL,*data=NULL;cJSON *root=NULL;
 if(!current(r->generation)||!sol_template||!g_cfg.sol_key[0])return false;
 cJSON *req=cJSON_Duplicate(sol_template,true);
 if(!req)return false;
 if(!cJSON_AddStringToObject(req,"input",r->text))goto payload_done;
 if(sol_previous[0]&&!cJSON_AddStringToObject(req,"previous_response_id",sol_previous))goto payload_done;
 body=cJSON_PrintUnformatted(req);
 payload_done:cJSON_Delete(req);if(!body)return false;
 data=heap_caps_malloc(32768,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!data){free(body);return false;}
 /* Avoid reusing sockets after a long user pause/server idle timeout. */
 if(sol_http&&bober_now_ms()-sol_http_used_ms>20000)sol_http_drop();
 bool reused=sol_http!=0;
 esp_http_client_config_t cfg={.url="https://api.openai.com/v1/responses",.method=HTTP_METHOD_POST,
  .timeout_ms=15000,.buffer_size=2048,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,
  .keep_alive_enable=true};
 if(!sol_http)sol_http=esp_http_client_init(&cfg);
 esp_http_client_handle_t h=sol_http;
 if(!h){free(body);free(data);return false;}
 char auth[540];snprintf(auth,sizeof(auth),"Bearer %s",g_cfg.sol_key);
 esp_http_client_set_header(h,"Authorization",auth);memset(auth,0,sizeof(auth));
 esp_http_client_set_header(h,"Content-Type","application/json");
 esp_http_client_set_header(h,"Accept","application/json");
 esp_http_client_set_header(h,"Connection","keep-alive");
 int64_t start=bober_now_ms(),deadline=start+45000;
 if(!current(r->generation))goto done;
 if(esp_http_client_open(h,strlen(body))!=ESP_OK){
  /* Open sends headers only, not the POST body. Retry a stale keep-alive
     connection once here, never after writing any body or generating tokens. */
  if(!reused||!current(r->generation))goto done;
  esp_http_client_close(h);reused=false;
  if(esp_http_client_open(h,strlen(body))!=ESP_OK)goto done;
 }
 for(size_t sent=0;sent<strlen(body);){
  if(!current(r->generation)||bober_now_ms()>deadline)goto done;
  int n=esp_http_client_write(h,body+sent,strlen(body)-sent);if(n<=0)goto done;sent+=(size_t)n;
 }
 if(esp_http_client_fetch_headers(h)<0)goto done;
 int status=esp_http_client_get_status_code(h);
 if(status!=200){ESP_LOGE(TAG,"Sol HTTP status=%d",status);goto done;}
 size_t used=0;
 while(current(r->generation)&&bober_now_ms()<deadline){
  if(used>=32767)goto done;
  int n=esp_http_client_read(h,data+used,32767-used);
  if(n<0)goto done;
  if(n>0){used+=(size_t)n;continue;}
  if(esp_http_client_is_complete_data_received(h))break;
  vTaskDelay(pdMS_TO_TICKS(10));
 }
 if(!current(r->generation)||!esp_http_client_is_complete_data_received(h))goto done;
 data[used]=0;root=cJSON_Parse(data);
 if(strcmp(str(root,"status"),"completed")||strcmp(str(root,"service_tier"),"ultrafast"))goto done;
 const char *id=str(root,"id");if(!id[0]||strlen(id)>=sizeof(sol_previous))goto done;
 bool ending=false;char farewell[513]={0};size_t text_used=0;
 r->text[0]=0;
 cJSON *item,*part;
 cJSON_ArrayForEach(item,cJSON_GetObjectItem(root,"output")){
  const char *type=str(item,"type");
  if(!strcmp(type,"reasoning"))continue;
  if(!strcmp(type,"function_call")){
   if(ending||strcmp(str(item,"name"),"end_conversation"))goto done;
   cJSON *args=cJSON_Parse(str(item,"arguments"));const char *bye=str(args,"farewell");size_t n=strlen(bye);
   bool valid=n>0&&n<sizeof(farewell)&&strspn(bye," \t\r\n")<n;
   if(valid)memcpy(farewell,bye,n+1);
   cJSON_Delete(args);
   if(!valid)goto done;
   ending=true;continue;
  }
  if(strcmp(type,"message"))goto done;
  cJSON_ArrayForEach(part,cJSON_GetObjectItem(item,"content")){
   if(strcmp(str(part,"type"),"output_text"))goto done;
   const char *text=str(part,"text");size_t n=strlen(text);
   if(text_used+n+2>=sizeof(r->text))goto done;
   if(text_used)r->text[text_used++]=' ';
   memcpy(r->text+text_used,text,n+1);text_used+=n;
  }
 }
 if(ending)strlcpy(r->text,farewell,sizeof(r->text));
 if(!r->text[0]||!current(r->generation))goto done;
 strlcpy(sol_previous,id,sizeof(sol_previous));
 if(ending)atomic_store(&end_after_reply,true);
 sol_last_activity_ms=bober_now_ms();
 sol_http_used_ms=bober_now_ms();
 ESP_LOGI(TAG,"Sol completed: tier=ultrafast elapsed_ms=%lld reused_client=%d end=%d",(long long)(bober_now_ms()-start),reused,ending);
 ok=true;
 done:cJSON_Delete(root);free(data);free(body);if(!ok)sol_http_drop();return ok;
}
