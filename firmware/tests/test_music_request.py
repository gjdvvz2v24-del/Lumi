#!/usr/bin/env python3
"""Verify requested brief preservation, cheerful fallback styles and strict argument validation."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
idf=Path(os.environ.get('IDF_PATH','/Users/grpi/Work/lumi/esp-idf'))
s=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "music_protocol.h"
int main(void){
 const char *request="{\"prompt\":\"O Zosi, jej czerwonym rowerze i wycieczce nad jezioro. Refren: hop, hop!\",\"style\":\"\"}";
 for(unsigned i=0;i<64;i++){
  cJSON *p=lumi_music_payload(request,i);assert(p);
  const char *text=cJSON_GetObjectItem(p,"prompt")->valuestring;
  assert(strstr(text,"czerwonym rowerze")&&strstr(text,"nad jezioro")&&strstr(text,"hop, hop!"));
  assert(strstr(text,"cheerful, lively and playful")&&!strstr(text,"Subject:"));
  assert(cJSON_GetObjectItem(p,"music_length_ms")->valueint==60000);cJSON_Delete(p);
 }
 cJSON *p=lumi_music_payload("{\"prompt\":\"O tym czerwonym rowerze Zosi, który omawialiśmy\",\"style\":\"powolny smutny rock\"}",42);assert(p);
 const char *text=cJSON_GetObjectItem(p,"prompt")->valuestring;
 assert(strstr(text,"Musical style: powolny smutny rock")&&!strstr(text,"Keep the music cheerful"));cJSON_Delete(p);
 assert(!lumi_music_payload("{}",0));assert(!lumi_music_payload("{\"prompt\":\"   \",\"style\":\"\"}",0));
 assert(!lumi_music_payload("{\"prompt\":\"ok\"}",0));
 assert(!lumi_music_payload("{\"prompt\":\"ok\",\"style\":9}",0));
 assert(!lumi_music_payload("{\"prompt\":\"ok\",\"style\":\"\",\"extra\":true}",0));
 char long_text[1800];memset(long_text,'x',sizeof(long_text)-1);long_text[sizeof(long_text)-1]=0;
 cJSON *args=cJSON_CreateObject();cJSON_AddStringToObject(args,"prompt",long_text);cJSON_AddStringToObject(args,"style","");assert(!lumi_music_request_valid(args));cJSON_Delete(args);
 cJSON *tool=lumi_music_tool();assert(!strcmp(cJSON_GetObjectItem(tool,"name")->valuestring,"play_song"));
 cJSON *required=cJSON_GetObjectItem(cJSON_GetObjectItem(tool,"parameters"),"required");assert(cJSON_GetArraySize(required)==2);cJSON_Delete(tool);
 puts("PASS: requested topic/details retained, cheerful random style only when unspecified, explicit style wins, invalid/oversized briefs rejected");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(s)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(root/'main'),'-I'+str(idf/'components/json/cJSON'),str(p/'test.c'),str(idf/'components/json/cJSON/cJSON.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
