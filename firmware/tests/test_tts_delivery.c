#include <assert.h>
#include <stdio.h>
#include "tts_delivery.h"
static const char *value(cJSON *j,const char *key){return cJSON_GetObjectItemCaseSensitive(j,key)->valuestring;}
int main(void){
 const char *speech="[excited] Cześć, Zosiu!";
 cJSON *j=lumi_tts_payload(speech);assert(j);
 assert(!strcmp(value(j,"text"),speech));assert(!strcmp(value(j,"model_id"),"eleven_v4_turbo"));
 assert(!strcmp(value(j,"language_code"),"pl"));cJSON_Delete(j);
 assert(!lumi_tts_is_singing("Wyjaśnij tag [sings]."));
 const char *mixed="[excited] [sings] Raz [whispers] dwa\n\n[happy] [singing] Trzy cztery!";
 j=lumi_tts_payload(mixed);assert(j);
 assert(!strcmp(value(j,"text"),"[sings] Raz  dwa\n[sings] Trzy cztery!"));
 assert(!strcmp(value(j,"model_id"),"eleven_v4"));assert(!cJSON_GetObjectItem(j,"language_code"));cJSON_Delete(j);
 j=lumi_tts_payload("[SINGS] In the moonlight\r\n[sad] Stars are bright");assert(j);
 assert(!strcmp(value(j,"text"),"[sings] In the moonlight\n[sings] Stars are bright"));cJSON_Delete(j);
 assert(!lumi_tts_payload("[sings] [excited]   \n"));
 assert(!lumi_tts_payload("[sings] lyrics [broken"));
 char huge[4096];memcpy(huge,"[sings]",7);for(int i=7;i<4095;i++)huge[i]=i%2?'a':'\n';huge[4095]=0;
 j=lumi_tts_payload(huge);assert(j);cJSON_Delete(j);
 puts("PASS: normal speech unchanged; exclusive singing tags; v4 routing; multilingual songs; bounds and malformed input");
}
