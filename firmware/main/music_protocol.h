#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#define LUMI_MUSIC_MS 60000u
#define LUMI_MUSIC_BYTES (32u * LUMI_MUSIC_MS)
#define LUMI_MUSIC_BRIEF_MAX 1600u
#define LUMI_MUSIC_STYLE_MAX 256u
static inline bool lumi_music_text_valid(const char *s,size_t max,bool empty){
 size_t n=strlen(s);if(n>max||(!empty&&(!n||strspn(s," \t\r\n")==n)))return false;
 for(size_t i=0;i<n;i++)if((unsigned char)s[i]<32&&s[i]!='\n'&&s[i]!='\r'&&s[i]!='\t')return false;
 return true;
}
static inline bool lumi_music_request_valid(cJSON *args){
 cJSON *brief=cJSON_GetObjectItemCaseSensitive(args,"prompt"),*style=cJSON_GetObjectItemCaseSensitive(args,"style");
 return cJSON_IsObject(args)&&cJSON_GetArraySize(args)==2&&cJSON_IsString(brief)&&cJSON_IsString(style)&&
  lumi_music_text_valid(brief->valuestring,LUMI_MUSIC_BRIEF_MAX,false)&&lumi_music_text_valid(style->valuestring,LUMI_MUSIC_STYLE_MAX,true);
}
/* Sol resolves the requested subject from the conversation. Only unspecified
   style is randomized locally, from cheerful genres; explicit style wins. */
static inline cJSON *lumi_music_payload(const char *request,uint32_t random){
 cJSON *args=cJSON_Parse(request);if(!lumi_music_request_valid(args)){cJSON_Delete(args);return NULL;}
 const char *brief=cJSON_GetObjectItemCaseSensitive(args,"prompt")->valuestring;
 const char *style=cJSON_GetObjectItemCaseSensitive(args,"style")->valuestring;
 static const char *happy[]={"upbeat acoustic pop","bouncy ska","cheerful brass funk","bright synth pop","lively folk with accordion","playful swing jazz","upbeat disco","energetic guitar pop-rock","sunny reggae","cheerful bossa nova","jolly sea shanty","upbeat melodic hip-hop","lively bluegrass","joyful electronic dance","playful Latin pop","cheerful ukulele pop"};
 bool specified=strspn(style," \t\r\n")<strlen(style);
 char prompt[2800];
 int n=snprintf(prompt,sizeof(prompt),"Create an original complete 60-second song following this conversation-derived song brief. Preserve its requested subject, characters, story details and any explicit musical directions. Song brief: %s\nMusical style: %s. %s Unless the brief explicitly requests otherwise, use clear female vocals singing original Polish lyrics with accompaniment. Start singing promptly and finish musically within 60 seconds. Invent original lyrics, do not quote existing songs or imitate named artists.",brief,specified?style:happy[random%16],specified?"Follow the requested style and mood.":"Keep the music cheerful, lively and playful.");
 cJSON *j=NULL;
 if(n>0&&(size_t)n<sizeof(prompt)){
  j=cJSON_CreateObject();
  if(j&&(!cJSON_AddStringToObject(j,"prompt",prompt)||!cJSON_AddStringToObject(j,"model_id","music_v2")||
    !cJSON_AddNumberToObject(j,"music_length_ms",LUMI_MUSIC_MS)||!cJSON_AddBoolToObject(j,"force_instrumental",false))){cJSON_Delete(j);j=NULL;}
 }
 cJSON_Delete(args);return j;
}
static inline cJSON *lumi_music_tool(void){
 return cJSON_Parse("{\"type\":\"function\",\"name\":\"play_song\",\"description\":\"Generate and play one original 60-second song through ElevenLabs Music when Zosia asks for singing or a song. Resolve the requested topic and references from her latest request AND relevant preceding conversation (e.g. about that, about her, same subject). Put a concise self-contained song brief in prompt, preserving requested characters, events and details. Do not send the whole conversation or unrelated personal details. Never replace her topic with a random subject. If no topic can be inferred, ask one short question instead of calling this tool. In style copy ONLY a style, genre or mood explicitly requested by Zosia, otherwise use an empty string: firmware will choose a cheerful random style. Do not invent a genre in prompt when none was requested. Use this tool instead of speaking lyrics. Do not call for discussion, quotations or negated requests.\",\"strict\":true,\"parameters\":{\"type\":\"object\",\"properties\":{\"prompt\":{\"type\":\"string\",\"description\":\"Self-contained song brief from the request and relevant conversation, up to 1600 UTF-8 bytes. Preserve topic and concrete details.\"},\"style\":{\"type\":\"string\",\"description\":\"Explicitly requested musical style/genre/mood, up to 256 UTF-8 bytes; empty string if not specified.\"}},\"required\":[\"prompt\",\"style\"],\"additionalProperties\":false}}");
}
