#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "cJSON.h"
#define LUMI_MUSIC_MS 60000u
#define LUMI_MUSIC_BYTES (32u * LUMI_MUSIC_MS)
/* No user text, artist names or existing lyrics are sent to Music. Each request
   picks its own combination; musical variation is also generated server-side. */
static inline cJSON *lumi_music_payload(uint32_t random){
 static const char *styles[]={"acoustic pop","bouncy ska","gentle waltz","funk with brass","synth pop","folk with accordion","swing jazz","disco","guitar rock","reggae","bossa nova","sea shanty","hip-hop with melodic singing","cinematic ballad","bluegrass","electronic dance"};
 static const char *topics[]={
  "a school day when every lesson becomes a surprise", "a homemade rocket and its first mission", "baking pancakes for unexpected guests", "a football team's impossible comeback",
  "a friendship formed on a long train journey", "a robot learning how to tell jokes", "finding a treasure map in an old library", "a skateboarder finally landing a new trick",
  "the secret nightlife of kitchen utensils", "stage fright before a first concert", "a video game team solving a puzzle together", "a camping trip interrupted by a thunderstorm",
  "an umbrella that dreams of sunny weather", "a bicycle adventure without a destination", "a garden that grows musical instruments", "a detective investigating missing homework",
  "making pizza aboard a space station", "a tiny bakery with a very unusual customer", "a goalkeeper who wants to become a dancer", "an ordinary lift that visits imaginary floors",
  "a family trying to take one perfect holiday photo", "the first snow transforming a familiar street", "a racing car that prefers sightseeing", "a wizard who can only do useful household magic",
  "losing a phone and rediscovering the neighbourhood", "building a cardboard city with friends", "a mountain hike with a surprising view", "a brave little boat crossing a huge puddle",
  "a museum whose exhibits swap places at night", "a cooking contest with one ingredient missing", "a band rehearsing in a noisy garage", "a secret door behind a school locker",
  "an alarm clock that would like a day off", "being new somewhere and making a first friend", "a race against time to catch the last bus", "a beach holiday with a completely wrong suitcase",
  "a supermarket trolley going on an adventure", "an inventor fixing a machine that produces bubbles", "a shy dragon joining a school choir", "a message in a bottle travelling between islands",
  "a rainy afternoon turning into a living-room expedition", "a young astronaut missing food from home", "learning a difficult dance without giving up", "a magic paintbrush changing the town's colours",
  "a picnic where the sandwiches go missing", "a snowman planning a summer holiday", "a birthday surprise almost being discovered", "a paper aeroplane delivering an important message",
  "a computer keyboard trying to compose a poem", "a neighbourhood starting its own Olympics", "a lighthouse guiding someone home", "a dog appointed temporary mayor of a town",
  "a train conductor collecting unusual stories", "a lost sock returning with travel souvenirs", "a deep-sea expedition finding a hidden city", "a group of friends designing a ridiculous invention",
  "a thundercloud learning to play the drums", "a sleepover turning into a mystery investigation", "a cat accidentally joining a theatre performance", "a brave decision that begins a new adventure",
  "a shopping list that turns into a treasure hunt", "a secret rooftop garden above a busy city", "a school sports day where teamwork wins", "a future city where everyone travels differently"
 };
 static const char *settings[]={"a bustling small town","a seaside port","a futuristic city","a quiet village","a school and its surroundings","a mountain valley","a summer holiday","a snowy evening","an ordinary home","a colourful festival","a forest campsite","a busy railway station","an island","a city park","a weekend trip","a place invented especially for this song"};
 static const char *angles[]={"an unexpected discovery","a small problem with an inventive solution","a friendly rivalry","a plan that goes hilariously wrong","teamwork","a surprising change of perspective","finding courage","a misunderstanding that gets resolved","a race against time","a secret revealed in the chorus","a chain of amusing coincidences","an everyday moment becoming an adventure","a journey with a memorable ending","trying something for the first time","a clever reversal at the end","a funny narrator with strong opinions"};
 static const char *moods[]={"energetic and cheeky","warm and adventurous","dreamy and curious","dramatic but funny","relaxed and witty","triumphant and upbeat","mysterious and playful","gentle and hopeful"};
 /* Keep the last six subjects out of the next pick, even if the hardware
    random source repeats. No extra LLM or paid generation call is needed. */
 static unsigned recent[6],count;
 unsigned topic=random%64;
 for(unsigned tries=0;tries<64;tries++){
  bool seen=false;for(unsigned i=0;i<count;i++)if(recent[i]==topic)seen=true;
  if(!seen)break;
  topic=(topic+17)%64;
 }
 for(unsigned i=5;i>0;i--){recent[i]=recent[i-1];}
 recent[0]=topic;if(count<6)count++;
 char prompt[1200];
 snprintf(prompt,sizeof(prompt),"An original complete 60-second song. Subject: %s. Setting: %s. Narrative angle: %s. Genre: %s. Mood: %s. Make the subject the focus, with specific fresh details and a clear little story. Clear expressive female vocals singing entirely new Polish lyrics, with musical accompaniment, suitable for children. Start singing promptly and finish musically within 60 seconds. Invent all lyrics; no quotations or imitation of artists. Do not fall back to generic songs about penguins or axolotls.",topics[topic],settings[(random/1024)%16],angles[(random/16384)%16],styles[(random/64)%16],moods[(random/262144)%8]);
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
