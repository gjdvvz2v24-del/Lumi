#pragma once
#include <stdint.h>
#include <stdio.h>
#include "cJSON.h"
#define LUMI_MUSIC_MS 60000u
#define LUMI_MUSIC_BYTES (32u * LUMI_MUSIC_MS)
/* No user text, artist names or existing lyrics are sent to Music. Each request
   picks its own combination; musical variation is also generated server-side. */
static inline cJSON *lumi_music_payload(uint32_t random){
 static const char *styles[]={"acoustic pop","bouncy ska","gentle waltz","funk with brass","playful synth pop","folk with accordion","swing jazz","upbeat disco"};
 static const char *themes[]={"an axolotl exploring the moon","a dancing potato at a picnic","a sleepy dragon looking for socks","a cloud opening a bakery","a tiny submarine visiting a jellyfish","a penguin learning to ride a bicycle","a comet with hiccups","a snail winning a space race"};
 char prompt[640];
 snprintf(prompt,sizeof(prompt),"An original complete 60-second %s song about %s. Clear expressive female vocals singing new Polish lyrics, with musical accompaniment. Funny, playful, suitable for children. Start singing promptly and finish with a musical ending within 60 seconds. Invent all lyrics; no quotations or imitation of artists.",styles[random%8],themes[(random/8)%8]);
 cJSON *j=cJSON_CreateObject();if(!j)return NULL;
 if(!cJSON_AddStringToObject(j,"prompt",prompt)||!cJSON_AddStringToObject(j,"model_id","music_v2")||
    !cJSON_AddNumberToObject(j,"music_length_ms",LUMI_MUSIC_MS)||!cJSON_AddBoolToObject(j,"force_instrumental",false)){
  cJSON_Delete(j);return NULL;
 }
 return j;
}
static inline cJSON *lumi_music_tool(void){
 return cJSON_Parse("{\"type\":\"function\",\"name\":\"play_random_song\",\"description\":\"Generate and play one original random 60-second song with Polish vocals through ElevenLabs Music when the user asks you to sing something or asks for a song, e.g. zaśpiewaj coś, zaśpiewaj piosenkę, chcę piosenkę, zagraj piosenkę. Use this instead of writing or speaking lyrics. Do not call for discussion about songs, questions about singing, quotations or a negated request. The topic and style are random.\",\"strict\":true,\"parameters\":{\"type\":\"object\",\"properties\":{},\"required\":[],\"additionalProperties\":false}}");
}
