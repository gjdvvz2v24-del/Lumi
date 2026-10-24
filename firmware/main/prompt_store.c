#include "prompt_store.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#define PROMPT_MAX_BYTES 15000
#ifndef BOBER_PROMPT_PATH
#define BOBER_PROMPT_PATH "/bober_prompt/prompt.txt"
#endif
static const char *TAG="prompt";

static bool valid_text(const unsigned char *s,size_t n){
 bool nonblank=false;
 for(size_t i=0;i<n;){
  unsigned c=s[i++];
  if(c<0x80){
   if(c<32&&c!='\n'&&c!='\r'&&c!='\t')return false;
   if(c>32)nonblank=true;
   continue;
  }
  nonblank=true;unsigned count,value,min;
  if(c>=0xc2&&c<=0xdf){count=1;value=c&31;min=0x80;}
  else if(c>=0xe0&&c<=0xef){count=2;value=c&15;min=0x800;}
  else if(c>=0xf0&&c<=0xf4){count=3;value=c&7;min=0x10000;}
  else return false;
  if(i+count>n)return false;
  while(count--){c=s[i++];if((c&0xc0)!=0x80)return false;value=(value<<6)|(c&63);}
  if(value<min||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
 }
 return nonblank;
}
char *bober_prompt_session(const char *fallback_session){
 bool mounted=false;
 {
  esp_vfs_spiffs_conf_t cfg={.base_path="/bober_prompt",.partition_label="promptfs",.max_files=2,.format_if_mount_failed=false};
  mounted=esp_vfs_spiffs_register(&cfg)==ESP_OK;
 }
 char *prompt=NULL;size_t n=0;
 if(mounted){
  FILE *f=fopen(BOBER_PROMPT_PATH,"rb");
  if(f){
   prompt=malloc(PROMPT_MAX_BYTES+2);
   if(prompt){
    n=fread(prompt,1,PROMPT_MAX_BYTES+1,f);
    if(ferror(f)||n>PROMPT_MAX_BYTES||!valid_text((unsigned char*)prompt,n)){free(prompt);prompt=NULL;}
    else prompt[n]=0;
   }
   fclose(f);
  }
 }
 if(mounted)esp_vfs_spiffs_unregister("promptfs");
 if(!prompt){
  ESP_LOGW(TAG,"prompt.txt unavailable/invalid; using compiled fallback");
  return strdup(fallback_session);
 }
 cJSON *j=cJSON_Parse(fallback_session);
 cJSON *session=cJSON_GetObjectItemCaseSensitive(j,"session");
 cJSON *value=cJSON_CreateString(prompt);free(prompt);
 if(!session||!value){cJSON_Delete(value);cJSON_Delete(j);return NULL;}
 if(!cJSON_ReplaceItemInObjectCaseSensitive(session,"instructions",value)){cJSON_Delete(value);cJSON_Delete(j);return NULL;}
 char *result=cJSON_PrintUnformatted(j);cJSON_Delete(j);
 if(result&&strlen(result)>=32768){free(result);return NULL;}
 if(result)ESP_LOGI(TAG,"Using prompt.txt from flash (%u bytes)",(unsigned)n);
 return result;
}
