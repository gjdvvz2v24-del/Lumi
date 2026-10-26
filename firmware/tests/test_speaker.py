#!/usr/bin/env python3
"""Execute production voice playback with fake I2S; check silence and shutdown races."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
speaker='void bober_speaker_task(void *arg)'+(root/'main/audio.c').read_text().split('void bober_speaker_task(void *arg)',1)[1]
prefix=r'''
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#include <stdatomic.h>
#include "offline_snore.h"
static atomic_bool offline_sound_busy;
static const uint8_t snore_start[2]={0};
#define snore_end (snore_start+2)
#include <stdio.h>
#define ESP_LOGI(...) ((void)0)
#define BOBER_POWER_SPEAKER 2
#define BOBER_SPEAKING 2
#define pdMS_TO_TICKS(x) (x)
static bool power,amp,g_ws_ready=true,g_session_enabled=true,g_wifi_ready=true;
static uint32_t g_generation=1;
static int g_phase=2,tx,stage,pops;
static int64_t g_last_play_ms;
static uint8_t g_speaker_level;
static jmp_buf done;
static bool bober_power_requested(void){return power;}
static void bober_power_checkpoint(uint32_t bit){assert(bit==2&&!amp);longjmp(done,1);}
static void set_amp(bool on){amp=on;}
static int64_t bober_now_ms(void){return 100;}
static void vTaskDelay(unsigned t){(void)t;}
static bool lumi_reaction_busy(void){return stage==2||stage==3;}
static size_t lumi_reaction_read(uint8_t *out,size_t len,bool enabled,uint32_t now){
 (void)len;(void)now;if(!enabled)return 0;
 if(stage==2){int16_t p=1000;memcpy(out,&p,2);return 2;}return 0;
}
static size_t bober_pcm_pop(uint8_t *out,size_t len){
 assert(len==640);pops++;int16_t pcm[2]={-32768,32767};memcpy(out,pcm,4);
 if(stage==5){g_generation++;g_session_enabled=false;}
 return 4;
}
static void i2s_channel_write(int ch,const void *data,size_t len,size_t *written,unsigned timeout){
 (void)ch;(void)timeout;assert(len==2560);const int32_t *pcm=data;
 if(stage==0){assert(amp&&pcm[0]==INT32_MIN&&pcm[1]==INT32_MIN&&pcm[2]==2147418112);for(int i=4;i<640;i++)assert(!pcm[i]);g_phase=3;}
 else if(stage==2){assert(amp&&pcm[0]==65536000&&pcm[1]==65536000);for(int i=2;i<640;i++)assert(!pcm[i]);assert(pops==1);}
 else if(stage==3){assert(amp&&pops==1);for(int i=0;i<640;i++)assert(!pcm[i]);}
 else if(stage==4){assert(amp&&pops==2&&pcm[0]==INT32_MIN);}
 else {assert(!amp);for(int i=0;i<640;i++)assert(!pcm[i]);}
 if(stage==1){assert(pops==1);g_phase=2;}
 if(stage==5){assert(pops==3);power=true;}
 stage++;*written=len;
}
'''
main=r'''
int main(void){if(!setjmp(done))bober_speaker_task(NULL);assert(stage==6&&!amp&&pops==3);puts("PASS: signed PCM, silence, reaction pauses TTS, echo tail, TTS resumes, generation invalidation and sleep");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(prefix+speaker+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'main'),str(root/'main/offline_snore.c'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
