#pragma once
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "cJSON.h"
#include "openai_profile.h"

/* Only leading delivery tags select singing. A quoted tag later in ordinary
   conversation must not switch the voice to song. */
static inline bool lumi_tts_is_singing(const char *text){
 const char *p=text;
 while(*p){
  while(isspace((unsigned char)*p))p++;
  if(*p!='[')return false;
  const char *end=strchr(p+1,']');if(!end)return false;
  size_t n=(size_t)(end-p-1);
  if((n==5&&!strncasecmp(p+1,"sings",5))||
     (n==7&&!strncasecmp(p+1,"singing",7)))return true;
  p=end+1;
 }
 return false;
}

/* Preserve the lyrics and line boundaries, remove all conflicting bracketed
   directions, then put exactly one singing tag before each nonempty line. */
static inline char *lumi_tts_song_text(const char *text){
 size_t n=strlen(text);if(n>4095)return NULL;
 char *out=malloc(n*8+8);if(!out)return NULL;
 size_t used=0;bool line_start=true;
 for(const char *p=text;*p;){
  if(*p=='['){
   const char *end=strchr(p+1,']');
   if(!end){free(out);return NULL;}
   p=end+1;continue;
  }
  if(*p=='\r'||*p=='\n'){
   if(!line_start){while(used&&out[used-1]==' ')used--;out[used++]='\n';}
   line_start=true;p++;continue;
  }
  if(line_start&&isspace((unsigned char)*p)){p++;continue;}
  if(line_start){memcpy(out+used,"[sings] ",8);used+=8;line_start=false;}
  out[used++]=*p++;
 }
 while(used&&isspace((unsigned char)out[used-1]))used--;
 out[used]=0;
 if(!used){free(out);return NULL;}
 return out;
}

static inline cJSON *lumi_tts_payload(const char *text){
 bool singing=lumi_tts_is_singing(text);
 char *song=singing?lumi_tts_song_text(text):NULL;
 if(singing&&!song)return NULL;
 cJSON *j=cJSON_CreateObject();
 if(!j){free(song);return NULL;}
 bool ok=cJSON_AddStringToObject(j,"text",song?song:text)&&
         cJSON_AddStringToObject(j,"model_id",singing?BOBER_SINGING_MODEL:BOBER_TTS_MODEL);
 /* Leave song language detection to the multilingual model; an explicit
    Polish override would also apply to songs requested in another language. */
 if(ok&&!singing)ok=cJSON_AddStringToObject(j,"language_code","pl")!=NULL;
 free(song);
 if(!ok){cJSON_Delete(j);return NULL;}
 return j;
}
