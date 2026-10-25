#!/usr/bin/env python3
"""Exercise actual NVS loading: old Bober voice migrates without losing credentials."""
from pathlib import Path
import json,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
a=json.loads((r/'agent-profile.json').read_text())
assert '#define BOBER_DEFAULT_VOICE '+json.dumps(a['tts']['voice_id']) in (r/'main/voice_profile.h').read_text()
s=(r/'main/main.c').read_text();load=s[s.index('static void get('):s.index('void bober_pcm_reset')]
source=r'''
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include "voice_profile.h"
#define ESP_OK 0
#define NVS_READONLY 0
typedef int nvs_handle_t;
static int scenario;
static struct { char ssid[33],password[65],openai_key[512],api_key[192],voice_id[96],sol_key[512]; } g_cfg;
static int nvs_open(const char *ns,int mode,nvs_handle_t *n){(void)ns;(void)mode;*n=1;return scenario==0?-1:0;}
static void nvs_close(nvs_handle_t n){(void)n;}
static int nvs_get_str(nvs_handle_t n,const char *key,char *out,size_t *size){
 (void)n;const char *value="test-credential";
 if(!strcmp(key,"voice")){
  if(scenario==1)return -1;
  value=scenario==2?"gNNJUEmXjdMOywK9rhxY":scenario==3?BOBER_DEFAULT_VOICE:"custom-voice";
 }
 strlcpy(out,value,*size);return 0;
}
'''+load+r'''
int main(void){
 for(scenario=0;scenario<5;scenario++){
  load();assert(!strcmp(g_cfg.voice_id,scenario==4?"custom-voice":BOBER_DEFAULT_VOICE));
  if(scenario){assert(!strcmp(g_cfg.ssid,"test-credential"));assert(!strcmp(g_cfg.openai_key,"test-credential"));assert(!strcmp(g_cfg.api_key,"test-credential"));assert(!strcmp(g_cfg.sol_key,"test-credential"));}
 }
 puts("PASS: Lumi default, missing NVS, old Bober migration, saved Lumi/custom voice and retained credentials");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(r/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
# Actual generated constants must carry the complete new prompt and greeting.
header=(r/'main/openai_profile.h').read_text()
line=next(x for x in header.splitlines() if x.startswith('static const char bober_session_update'))
session=json.loads(json.loads(line.split(' = ',1)[1].removesuffix(';')))['session']
assert session['instructions']==(r/'prompt.txt').read_text().strip()
assert 'Jesteś Lumi' in session['instructions'] and 'Jesteś Bober' not in session['instructions']
assert session['tools']==a['tools'] and session['tool_choice']=='auto' and session['audio']['input']['transcription']['language']=='pl'
line=next(x for x in header.splitlines() if x.startswith('static const char bober_greeting'))
assert json.loads(line.split(' = ',1)[1].removesuffix(';'))==a['first_message']
print('PASS: generated Lumi prompt, greeting, Polish ASR and end-conversation tool')

assert "ciekawostki o kupie" in session["instructions"]
assert a["sol"]["reasoning_effort"]=="medium" and a["sol"]["max_output_tokens"]==2048
