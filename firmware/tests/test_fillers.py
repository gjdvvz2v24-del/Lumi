#!/usr/bin/env python3
"""Offline boundary checks for filler frequency and bundled PCM assets."""
import json, os, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=r'''
#include <assert.h>
#include "filler_policy.h"
int main(void){
 const char *long_text="jeden dwa trzy cztery piec szesc siedem osiem dziewiec dziesiec jeden dwa trzy cztery piec szesc siedem osiem dziewiec dwadziescia";
 assert(!lumi_filler_eligible("krótkie zdanie",100000,0,0));
 assert(lumi_filler_eligible(long_text,100000,0,0));
 assert(!lumi_filler_eligible(long_text,100000,0,1));
 assert(!lumi_filler_eligible(long_text,100000,0,2));
 assert(!lumi_filler_eligible(long_text,159999,100000,0));
 assert(lumi_filler_eligible(long_text,160000,100000,0));
 assert(!lumi_filler_eligible(long_text,99999,100000,0));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
folder=root/'main/assets/fillers'
manifest=json.loads((folder/'manifest.json').read_text())
assert len(manifest['texts'])==6
for i in range(6):
 audio=(folder/f'filler_{i}.pcm').read_bytes()
 assert 0<len(audio)<=96000 and len(audio)%2==0
profile=json.loads((root/'agent-profile.json').read_text())
assert manifest['voice_id']==profile['tts']['voice_id']
assert manifest['model_id']==profile['tts']['model_id']
print('PASS: long utterances, probability, cooldown boundaries, six matching PCM assets')

# Run actual orchestration with a deterministic fake scheduler, without networking.
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include "filler_policy.h"
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
typedef struct {uint32_t generation;char text[4096];} reply_t;
static bool active=true,g_ws_reconnect;
static int64_t clock_ms=100000,g_last_pcm_ms,complete_at;
static unsigned pushes,joined;
static void (*worker)(void*);static void *worker_arg;
static int64_t bober_now_ms(void){return clock_ms;}
static bool current(uint32_t gen){return active&&gen==7;}
static uint32_t esp_random(void){return 0;}
static bool sol_generate(reply_t *r){strcpy(r->text,"real answer");joined++;return active;}
static void vTaskDelete(void *p){(void)p;}
static int xTaskCreate(void (*f)(void*),const char *name,int stack,void *arg,int prio,void *out){
 (void)name;(void)stack;(void)prio;(void)out;worker=f;worker_arg=arg;return 1;
}
static void vTaskDelay(int ms){clock_ms+=ms;if(worker&&clock_ms>=complete_at){void (*f)(void*)=worker;worker=0;f(worker_arg);}}
static bool bober_pcm_push(const uint8_t *p,size_t n,uint32_t gen){(void)p;assert(n&&gen==7&&active);pushes++;return true;}
'''
for i in range(6):
    assembly=f'.pushsection __TEXT,__const\n.globl _binary_filler_{i}_pcm_start\n_binary_filler_{i}_pcm_start:\n.space 320\n.globl _binary_filler_{i}_pcm_end\n_binary_filler_{i}_pcm_end:\n.popsection'
    harness+='__asm__('+json.dumps(assembly)+');\n'
harness+=r'''
#include "sol_fillers.inc"
static void long_turn(reply_t *r){r->generation=7;strcpy(r->text,"one two three four five six seven eight nine ten one two three four five six seven eight nine twenty");}
int main(void){
 reply_t r;long_turn(&r);complete_at=clock_ms+100;
 assert(sol_generate_with_filler(&r)&&joined==1&&pushes==0);
 long_turn(&r);complete_at=clock_ms+2000;
 assert(sol_generate_with_filler(&r)&&joined==2&&pushes==1);
 assert(!strcmp(r.text,"real answer"));
 long_turn(&r);complete_at=clock_ms+2000;
 assert(sol_generate_with_filler(&r)&&joined==3&&pushes==1);
 clock_ms+=60000;active=false;long_turn(&r);complete_at=clock_ms+2000;
 assert(!sol_generate_with_filler(&r)&&joined==4&&pushes==1&&!worker);
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'esp_random.h').write_text('')
    (p/'test.c').write_text(harness)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-I',str(p),'-I',str(root/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: actual orchestration skips fast answers, queues one cached clip, observes cooldown, joins on cancellation')
