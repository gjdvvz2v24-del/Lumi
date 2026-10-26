/* Cached acknowledgement is queued before the Music HTTP request. It plays
   while generation is pending, with no per-song TTS request or mic reopening. */
extern const uint8_t music_wait_start[] __asm__("_binary_music_wait_pcm_start");
extern const uint8_t music_wait_end[] __asm__("_binary_music_wait_pcm_end");
static bool queue_music_wait(uint32_t generation){
 const uint8_t *p=music_wait_start;int64_t start=bober_now_ms();
 while(p<music_wait_end){
  if(!current(generation)||g_ws_reconnect||bober_now_ms()-start>10000)return false;
  size_t n=(size_t)(music_wait_end-p);if(n>2048)n=2048;
  if(bober_pcm_push(p,n,generation)){p+=n;g_last_pcm_ms=bober_now_ms();}
  else vTaskDelay(pdMS_TO_TICKS(10));
 }
 return true;
}
/* Eleven Music PCM is signed 16-bit little-endian STEREO at the requested
   16 kHz (verified against a real 30 s response). TTS is mono. Downmix whole
   stereo frames, retaining up to three bytes between arbitrary HTTP chunks. */
static bool play_random_song(const reply_t *r){
 bool ok=false,bad=false;size_t received=0,carry=0;int status=0;
 int64_t start=bober_now_ms(),deadline=start+180000,last_progress=start,last_play=g_last_play_ms;
 const char *stage="open";
 cJSON *j=lumi_music_payload(esp_random());if(!j)return false;
 char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!body)return false;
 esp_http_client_config_t cfg={.url="https://api.elevenlabs.io/v1/music?output_format=pcm_16000",
  .method=HTTP_METHOD_POST,.timeout_ms=120000,.buffer_size=2048,
  .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.event_handler=header,.user_data=&bad};
 if(!music_archive_begin()){free(body);return false;}
 esp_http_client_handle_t h=esp_http_client_init(&cfg);if(!h){free(body);music_archive_discard();return false;}
 uint8_t stereo[2048],mono[1024];
 esp_http_client_set_header(h,"xi-api-key",g_cfg.api_key);
 esp_http_client_set_header(h,"Content-Type","application/json");esp_http_client_set_header(h,"Accept","audio/pcm");
 ESP_LOGI(TAG,"Music requested: model=music_v2 duration_ms=%u",LUMI_MUSIC_MS);
 if(!current(r->generation)||g_ws_reconnect)goto done;
 stage="announcement";if(!queue_music_wait(r->generation))goto done;
 stage="open";if(esp_http_client_open(h,strlen(body))!=ESP_OK)goto done;
 stage="write";
 for(size_t sent=0;sent<strlen(body);){
  if(!current(r->generation)||g_ws_reconnect||bober_now_ms()>deadline)goto done;
  int n=esp_http_client_write(h,body+sent,strlen(body)-sent);if(n<=0)goto done;sent+=(size_t)n;
 }
 stage="headers";
 if(esp_http_client_fetch_headers(h)<0)goto done;
 status=esp_http_client_get_status_code(h);if(status!=200){stage="http_status";goto done;}
 if(bad){stage="content_type";goto done;}
 last_progress=bober_now_ms();stage="read";
 while(current(r->generation)&&!g_ws_reconnect&&bober_now_ms()<deadline){
  if(bober_now_ms()-last_progress>30000){stage="no_progress";goto done;}
  int n=esp_http_client_read(h,(char*)stereo+carry,sizeof(stereo)-carry);if(n<0)goto done;
  if(!n){
   if(esp_http_client_is_complete_data_received(h)){
    ok=received==LUMI_MUSIC_BYTES*2u&&!carry;
    if(!ok)stage="invalid_duration_or_frame";
    break;
   }
   vTaskDelay(pdMS_TO_TICKS(10));continue;
  }
  received+=(size_t)n;last_progress=bober_now_ms();
  if(received>LUMI_MUSIC_BYTES*2u){stage="audio_limit_60s";goto done;}
  if(received==(size_t)n)ESP_LOGI(TAG,"Music first PCM: elapsed_ms=%lld",(long long)(bober_now_ms()-start));
  size_t total=carry+(size_t)n,frames=total/4;
  for(size_t i=0;i<frames;i++){
   int32_t left=(int16_t)((uint16_t)stereo[i*4]|((uint16_t)stereo[i*4+1]<<8));
   int32_t right=(int16_t)((uint16_t)stereo[i*4+2]|((uint16_t)stereo[i*4+3]<<8));
   uint16_t sample=(uint16_t)(int16_t)((left+right)/2);
   mono[i*2]=(uint8_t)sample;mono[i*2+1]=(uint8_t)(sample>>8);
  }
  if(frames&&!music_archive_append(mono,frames*2)){stage="archive_buffer";goto done;}
  while(frames&&!bober_pcm_push(mono,frames*2,r->generation)){
   if(g_last_play_ms!=last_play){last_play=g_last_play_ms;last_progress=bober_now_ms();}
   if(!current(r->generation)||g_ws_reconnect||bober_now_ms()>=deadline||bober_now_ms()-last_progress>30000){stage="backpressure";goto done;}
   vTaskDelay(pdMS_TO_TICKS(10));
  }
  if(frames)g_last_pcm_ms=bober_now_ms();
  carry=total-frames*4;memmove(stereo,stereo+frames*4,carry);
 }
 done:
 free(body);esp_http_client_close(h);esp_http_client_cleanup(h);
 if(ok){music_archive_ready=true;music_archive_tick();}
 else music_archive_discard();
 if(ok&&current(r->generation)&&!g_ws_reconnect){
  g_response_complete=true;sol_last_activity_ms=bober_now_ms();
  ESP_LOGI(TAG,"Music complete: audio_ms=%u elapsed_ms=%lld",LUMI_MUSIC_MS,(long long)(bober_now_ms()-start));return true;
 }
 ESP_LOGW(TAG,"Music interrupted: stage=%s status=%d received_bytes=%u elapsed_ms=%lld",stage,status,(unsigned)received,(long long)(bober_now_ms()-start));
 return false; /* Never retry a possibly billed generation automatically. */
}
