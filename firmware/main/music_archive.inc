/* Network-owner-only pending WAV. Allocate before paid generation; keep a
   completed file across session/Wi-Fi reconnects until Assets confirms storage.
   It is RAM, not flash: a power loss before upload completes loses this pending
   file. Never generate another song over an unsaved one. */
static uint8_t *music_archive_wav;
static size_t music_archive_used;
static bool music_archive_ready,music_archive_uncertain,music_archive_blocked;
static int64_t music_archive_retry_ms;
static char music_archive_name[96];
static void music_archive_discard(void){
 free(music_archive_wav);music_archive_wav=NULL;music_archive_used=0;
 music_archive_ready=music_archive_uncertain=false;
}
static void music_wav_u32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static bool music_archive_begin(void){
 music_archive_blocked=music_archive_wav!=NULL;
 if(music_archive_blocked)return false;
 music_archive_wav=heap_caps_malloc(LUMI_MUSIC_BYTES+44,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!music_archive_wav){ESP_LOGE(TAG,"Music archive allocation failed; generation not started");return false;}
 uint8_t *w=music_archive_wav;memset(w,0,44);
 memcpy(w,"RIFF",4);music_wav_u32(w+4,LUMI_MUSIC_BYTES+36);memcpy(w+8,"WAVEfmt ",8);
 music_wav_u32(w+16,16);w[20]=1;w[22]=1;music_wav_u32(w+24,16000);music_wav_u32(w+28,32000);
 w[32]=2;w[34]=16;memcpy(w+36,"data",4);music_wav_u32(w+40,LUMI_MUSIC_BYTES);
 time_t stamp=time(NULL);struct tm utc;gmtime_r(&stamp,&utc);char date[24];
 strftime(date,sizeof(date),"%Y%m%d-%H%M%SZ",&utc);
 snprintf(music_archive_name,sizeof(music_archive_name),"Lumi-%s-%08lx%08lx-60s.wav",date,(unsigned long)esp_random(),(unsigned long)esp_random());
 music_archive_used=0;music_archive_ready=music_archive_uncertain=false;music_archive_retry_ms=0;return true;
}
static bool music_archive_append(const uint8_t *pcm,size_t n){
 if(!music_archive_wav||n>LUMI_MUSIC_BYTES-music_archive_used)return false;
 memcpy(music_archive_wav+44+music_archive_used,pcm,n);music_archive_used+=n;return true;
}
static bool music_archive_online(void){return g_wifi_ready&&!bober_power_requested();}
static bool music_asset_write(esp_http_client_handle_t h,const void *data,size_t n,int64_t deadline){
 const char *p=data;
 while(n){
  if(!music_archive_online()||bober_now_ms()>=deadline)return false;
  size_t chunk=n>4096?4096:n;int written=esp_http_client_write(h,p,chunk);
  if(written<=0)return false;
  p+=written;n-=(size_t)written;
 }
 return true;
}
static cJSON *music_asset_response(esp_http_client_handle_t h,int64_t deadline){
 char *json=malloc(8192);if(!json)return NULL;size_t used=0;cJSON *out=NULL;
 while(music_archive_online()&&bober_now_ms()<deadline&&used<8191){
  int n=esp_http_client_read(h,json+used,8191-used);if(n<0)break;
  if(n){used+=(size_t)n;continue;}
  if(esp_http_client_is_complete_data_received(h)){json[used]=0;out=cJSON_Parse(json);break;}
  vTaskDelay(pdMS_TO_TICKS(10));
 }
 free(json);return out;
}
/* After an ambiguous POST, check the unique name before retrying upload. */
static bool music_asset_find(bool *exists){
 *exists=false;char url[256];snprintf(url,sizeof(url),"https://api.elevenlabs.io/v1/assets?search=%s&page_size=5",music_archive_name);
 esp_http_client_config_t cfg={.url=url,.method=HTTP_METHOD_GET,.timeout_ms=15000,.buffer_size=2048,
  .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
 esp_http_client_handle_t h=esp_http_client_init(&cfg);if(!h)return false;bool ok=false;
 esp_http_client_set_header(h,"xi-api-key",g_cfg.api_key);
 int64_t deadline=bober_now_ms()+30000;
 if(esp_http_client_open(h,0)==ESP_OK&&esp_http_client_fetch_headers(h)>=0&&esp_http_client_get_status_code(h)==200){
  cJSON *root=music_asset_response(h,deadline),*items=cJSON_GetObjectItem(root,"assets"),*item;
  ok=cJSON_IsArray(items);
  cJSON_ArrayForEach(item,items){if(!strcmp(str(item,"name"),music_archive_name)&&str(item,"asset_id")[0])*exists=true;}
  cJSON_Delete(root);
 }
 esp_http_client_close(h);esp_http_client_cleanup(h);return ok;
}
static void music_archive_tick(void){
 if(!music_archive_ready||!music_archive_online()||bober_now_ms()<music_archive_retry_ms)return;
 music_archive_retry_ms=bober_now_ms()+30000;
 if(music_archive_uncertain){
  bool exists=false;if(!music_asset_find(&exists))return;
  if(exists){ESP_LOGI(TAG,"Music archived (confirmed after retry): %s",music_archive_name);music_archive_discard();return;}
 }
 const char *boundary="LumiMusicAssetBoundary";char head[512],tail[80],ctype[96];
 int first=snprintf(head,sizeof(head),"--%s\r\nContent-Disposition: form-data; name=\"name\"\r\n\r\n%s\r\n--%s\r\nContent-Disposition: form-data; name=\"asset\"; filename=\"%s\"\r\nContent-Type: audio/wav\r\n\r\n",boundary,music_archive_name,boundary,music_archive_name);
 int last=snprintf(tail,sizeof(tail),"\r\n--%s--\r\n",boundary);
 snprintf(ctype,sizeof(ctype),"multipart/form-data; boundary=%s",boundary);
 if(first<=0||(size_t)first>=sizeof(head)||last<=0||(size_t)last>=sizeof(tail))return;
 esp_http_client_config_t cfg={.url="https://api.elevenlabs.io/v1/assets",.method=HTTP_METHOD_POST,
  .timeout_ms=15000,.buffer_size=2048,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
 esp_http_client_handle_t h=esp_http_client_init(&cfg);if(!h)return;
 esp_http_client_set_header(h,"xi-api-key",g_cfg.api_key);esp_http_client_set_header(h,"Content-Type",ctype);
 esp_http_client_set_header(h,"Accept","application/json");
 int64_t deadline=bober_now_ms()+60000;bool saved=false;int status=0;
 if(esp_http_client_open(h,first+44+LUMI_MUSIC_BYTES+last)!=ESP_OK)goto done;
 music_archive_uncertain=true;
 if(!music_asset_write(h,head,(size_t)first,deadline)||
    !music_asset_write(h,music_archive_wav,LUMI_MUSIC_BYTES+44,deadline)||
    !music_asset_write(h,tail,(size_t)last,deadline))goto done;
 if(esp_http_client_fetch_headers(h)<0)goto done;
 status=esp_http_client_get_status_code(h);if(status!=200&&status!=201)goto done;
 cJSON *root=music_asset_response(h,deadline);
 const char *id=str(root,"asset_id");saved=id[0]!=0;
 if(saved)ESP_LOGI(TAG,"Music archived: asset_id=%s name=%s bytes=%u",id,music_archive_name,(unsigned)(LUMI_MUSIC_BYTES+44));
 cJSON_Delete(root);
 done:esp_http_client_close(h);esp_http_client_cleanup(h);
 if(saved)music_archive_discard();
 else ESP_LOGW(TAG,"Music archive pending: status=%d; retry storage in 30 s, no new generation",status);
}
