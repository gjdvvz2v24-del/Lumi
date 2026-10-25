#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "cJSON.h"
#define LUMI_WIFI_NETWORKS 3
#define LUMI_WIFI_JSON_MAX 1400
typedef struct {char ssid[33];char password[64];} lumi_wifi_profile_t;
/* Bound parser recursion for this array-of-flat-objects schema. */
static inline bool lumi_wifi_json_shallow(const char *json){
 bool quoted=false,escaped=false;unsigned depth=0;
 for(const char *p=json;*p;p++){
  if(quoted){
   if(escaped){if(*p=='u'&&!strncmp(p+1,"0000",4))return false;escaped=false;}
   else if(*p=='\\')escaped=true;
   else if(*p=='"')quoted=false;
  }else if(*p=='"')quoted=true;
  else if(*p=='['||*p=='{'){if(++depth>2)return false;}
  else if(*p==']'||*p=='}'){if(!depth)return false;depth--;}
 }
 return !quoted&&!depth;
}
/* Validate the complete replacement before publishing or saving any profile. */
static inline bool lumi_wifi_profiles_parse(const char *json,lumi_wifi_profile_t out[LUMI_WIFI_NETWORKS]){
 if(!json||strlen(json)>LUMI_WIFI_JSON_MAX||!lumi_wifi_json_shallow(json))return false;
 const char *end=NULL;cJSON *root=cJSON_ParseWithOpts(json,&end,true);
 lumi_wifi_profile_t next[LUMI_WIFI_NETWORKS]={0};
 int count=cJSON_GetArraySize(root);bool ok=cJSON_IsArray(root)&&count>=1&&count<=LUMI_WIFI_NETWORKS;
 for(int i=0;ok&&i<count;i++){
  cJSON *item=cJSON_GetArrayItem(root,i),*ssid=cJSON_GetObjectItemCaseSensitive(item,"ssid"),*password=cJSON_GetObjectItemCaseSensitive(item,"password");
  if(!cJSON_IsObject(item)||!cJSON_IsString(ssid)||!cJSON_IsString(password)){ok=false;break;}
  size_t sn=strlen(ssid->valuestring),pn=strlen(password->valuestring);
  if(!sn||sn>32||pn>63||(pn&&pn<8)){ok=false;break;}
  for(const unsigned char *p=(const unsigned char*)ssid->valuestring;*p;p++)if(*p<32||*p==127)ok=false;
  for(const unsigned char *p=(const unsigned char*)password->valuestring;*p;p++)if(*p<32||*p==127)ok=false;
  for(int j=0;j<i;j++)if(!strcmp(next[j].ssid,ssid->valuestring))ok=false;
  if(ok){memcpy(next[i].ssid,ssid->valuestring,sn+1);memcpy(next[i].password,password->valuestring,pn+1);}
 }
 if(ok)memcpy(out,next,sizeof(next));
 memset(next,0,sizeof(next));cJSON_Delete(root);return ok;
}
