#!/usr/bin/env python3
"""Exercise the real offline scheduler and speaker path without network/device calls."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
asset=root/'main/assets/lumi_snore.pcm'
assert len(asset.read_bytes())==20800
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "offline_snore.h"
int main(int argc,char **argv){
 assert(argc==2);uint8_t sound[20800],out[640];FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(sound,1,sizeof(sound),f)==sizeof(sound));fclose(f);
 lumi_snore_state_t s={0};
 assert(!lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),true,true,0));
 assert(!lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),false,true,1000));
 assert(!lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),false,true,5999));
 size_t total=0;unsigned starts=0;uint32_t last_start=0;
 for(uint32_t now=6000;now<12000;now+=20){
  bool playing=s.playing;size_t offset=s.offset;
  size_t n=lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),false,true,now);
  if(n&&!playing){starts++;if(last_start)assert(now-last_start==5000);last_start=now;offset=0;}
  if(n){assert(!memcmp(out,sound+offset,n));total+=n;}
 }
 assert(starts==2&&total==2*sizeof(sound));
 /* Reconnect interrupts a running clip; retained echo tail only masks the mic. */
 assert(lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),false,true,16000)>0);
 assert(s.playing);
 assert(!lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),true,true,16020));
 assert(!s.playing&&!s.offline&&lumi_snore_busy(&s));
 assert(!lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),true,true,16200));assert(!lumi_snore_busy(&s));
 assert(!lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),true,true,60000));
 assert(!lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),false,true,60020));
 assert(!lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),false,true,65019));
 assert(lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),false,true,65020));
 assert(!lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),false,false,65040));assert(!s.playing);
 /* Unsigned arithmetic preserves the 5 s interval across the millis rollover. */
 memset(&s,0,sizeof(s));uint32_t begin=UINT32_MAX-2000;
 assert(!lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),false,true,begin));
 assert(!lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),false,true,begin+4999));
 assert(lumi_snore_read(&s,sound,sizeof(sound),out,sizeof(out),false,true,begin+5000));
 puts("PASS: full cached clips every 5 s, immediate reconnect cancellation, echo tail, priority, healthy silence, timer rollover");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'main'),str(root/'main/offline_snore.c'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(asset)],check=True)
# Execute the production speaker with Wi-Fi absent while the voice session is off.
audio=(root/'main/audio.c').read_text();speaker=audio[audio.index('void bober_speaker_task'):]
prefix=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "offline_snore.h"
#define ESP_LOGI(...) ((void)0)
#define BOBER_POWER_SPEAKER 2
#define BOBER_SPEAKING 2
#define pdMS_TO_TICKS(x) (x)
static atomic_bool offline_sound_busy;
static const uint8_t snore_start[1280]={1,1,1,1};
#define snore_end (snore_start+sizeof(snore_start))
static bool power,amp,g_ws_ready,g_session_enabled,g_wifi_ready;
static uint32_t g_generation=1,clock_ms;
static int g_phase,tx,heard;
static int64_t g_last_play_ms;
static uint8_t g_speaker_level;
static jmp_buf done;
static bool bober_power_requested(void){return power;}
static void bober_power_checkpoint(uint32_t bit){assert(bit==2&&!amp&&!atomic_load(&offline_sound_busy));longjmp(done,1);}
static void set_amp(bool on){amp=on;}
static int64_t bober_now_ms(void){return clock_ms;}
static void vTaskDelay(unsigned t){(void)t;}
static bool lumi_reaction_busy(void){return false;}
static size_t lumi_reaction_read(uint8_t *p,size_t n,bool enabled,uint32_t now){(void)p;(void)n;(void)enabled;(void)now;return 0;}
static size_t bober_pcm_pop(uint8_t *p,size_t n){(void)p;(void)n;assert(false);return 0;}
static void i2s_channel_write(int ch,const void *data,size_t len,size_t *written,unsigned timeout){
 (void)ch;(void)timeout;const int32_t *pcm=data;assert(len==2560);
 if(clock_ms<5000){assert(!amp);for(int i=0;i<640;i++)assert(!pcm[i]);}
 if(clock_ms==5000){assert(amp&&pcm[0]&&atomic_load(&offline_sound_busy));heard++;g_wifi_ready=true;}
 if(clock_ms==5020){assert(!amp);for(int i=0;i<640;i++)assert(!pcm[i]);}
 if(clock_ms==5200){assert(!amp&&!atomic_load(&offline_sound_busy));power=true;}
 clock_ms+=20;*written=len;
}
'''
main=r'''
int main(void){if(!setjmp(done))bober_speaker_task(NULL);assert(heard==1&&!amp);puts("PASS: real speaker plays offline with session disabled, cancels on Wi-Fi, masks echo and stops for power");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(prefix+speaker+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'main'),str(root/'main/offline_snore.c'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
