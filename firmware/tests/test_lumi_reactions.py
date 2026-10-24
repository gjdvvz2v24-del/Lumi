#!/usr/bin/env python3
"""Run real embedded sound reader with host critical-section mocks and PCM assets."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'freertos').mkdir()
 (p/'freertos/FreeRTOS.h').write_text('#pragma once\ntypedef int portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED 0\n#define portENTER_CRITICAL(p) ((void)(p))\n#define portEXIT_CRITICAL(p) ((void)(p))\n')
 s=(r/'main/reactions.c').read_text()
 for name in ('impact','shake'):
  raw=(r/f'main/assets/lumi_{name}.pcm').read_bytes();assert 0<len(raw)<=16000*2*4 and len(raw)%2==0
  s=s.replace(f'extern const uint8_t {name}_start[] asm("_binary_lumi_{name}_pcm_start");',f'static const uint8_t {name}_start[]={{'+','.join(map(str,raw))+'};')
  s=s.replace(f'extern const uint8_t {name}_end[] asm("_binary_lumi_{name}_pcm_end");',f'#define {name}_end ({name}_start+sizeof({name}_start))')
 (p/'test.c').write_text(s+r'''
#include <assert.h>
#include <stdio.h>
int main(void){
 uint8_t out[640];assert(!lumi_reaction_busy());
 assert(!lumi_reaction_read(out,sizeof(out),true,0));
 lumi_reaction_request(2);assert(!lumi_reaction_busy());
 for(int event=1;event<=1;event++){
  lumi_reaction_request(event);assert(lumi_reaction_busy());
  lumi_reaction_request(event==1?2:1); /* busy request cannot overwrite */
  size_t total=0,n;uint32_t t=0;
  const uint8_t *expected=event==1?impact_start:shake_start;
  while((n=lumi_reaction_read(out,sizeof(out),true,t))){
   assert(n%2==0&&n<=sizeof(out));assert(!memcmp(out,expected+total,n));total+=n;t+=20;
  }
  assert(total==(event==1?sizeof(impact_start):sizeof(shake_start)));
  assert(lumi_reaction_busy());lumi_reaction_read(out,sizeof(out),true,t+200);assert(!lumi_reaction_busy());
 }
 lumi_reaction_request(1);assert(lumi_reaction_busy());
 assert(!lumi_reaction_read(out,sizeof(out),false,10000));assert(!lumi_reaction_busy());
 assert(!lumi_reaction_read(out,sizeof(out),true,11000));
 lumi_reaction_request(0);assert(!lumi_reaction_busy());
 puts("PASS: impact PCM; shake requests ignored, busy suppression, exact samples, echo tail, cancellation, no replay after wake");
}
''')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),'-I'+str(r/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
