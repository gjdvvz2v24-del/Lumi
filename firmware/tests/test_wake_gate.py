"""Execute production microphone task with fake I2S and wake events."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
audio=(root/'main/audio.c').read_text()
mic=audio[audio.index('void bober_mic_task'):audio.index('void bober_speaker_task')]
helpers=audio[audio.index('static inline int16_t clamp'):audio.index('void bober_mic_task')]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdatomic.h>
static atomic_bool shake_wake_requested;
void bober_request_shake_wake(void);
#define ESP_OK 0
#define BOBER_POWER_MIC 1
#define pdMS_TO_TICKS(x) (x)
enum { BOBER_OFF, BOBER_CONNECTING, BOBER_SPEAKING, BOBER_LISTENING, BOBER_WAKE_WAIT=4 };
static bool g_session_enabled,g_ws_ready,g_ws_reconnect,power,healthy;
static int g_phase,rx,g_uplink=1,stage,reads,inits,deinits,processes,sends,resets,scenario;
static uint8_t g_mic_level;
static uint32_t g_generation;
static int32_t dc_prev_x,dc_prev_y;
static const char *TAG="test";
static jmp_buf done;
static void log_stub(const char *tag,const char *format,...){(void)tag;(void)format;}
#define ESP_LOGI(...) log_stub(__VA_ARGS__)
#define ESP_LOGW(...) log_stub(__VA_ARGS__)
#define ESP_LOGE(...) log_stub(__VA_ARGS__)
static int64_t bober_now_ms(void){return reads*20;}
static bool bober_power_requested(void){return power;}
static void bober_power_checkpoint(int bit){assert(bit==BOBER_POWER_MIC);if(power)longjmp(done,1);}
static bool lumi_reaction_busy(void){return false;}
static bool lumi_wake_init(void){inits++;healthy=scenario!=1;return healthy;}
static void lumi_wake_self_test(void){}
static bool lumi_capture_feed(const int16_t *pcm,size_t n){(void)pcm;(void)n;return false;}
static void lumi_wake_deinit(void){deinits++;healthy=false;}
static bool lumi_wake_healthy(void){return healthy;}
static unsigned lumi_wake_match(void){return 1;}
static float lumi_wake_score_for(unsigned model){(void)model;return 0.95f;}
static float lumi_wake_score(void){return 0.95f;}
static float lumi_wake_peak_score(void){return 0.99f;}
static float lumi_wake_signal_rms(void){return 100;}
static float lumi_wake_signal_ratio(void){return 0.01f;}
static bool lumi_wake_signal_ready(void){return true;}
static bool lumi_wake_process(const int16_t *pcm,size_t n){
 assert(!g_session_enabled&&g_phase==BOBER_WAKE_WAIT);assert(n==320);
 for(size_t i=0;i<n;++i)assert(pcm[i]==1000);
 processes++;if(scenario==2)healthy=false;
 return scenario==0&&(stage==1||stage==6);
}
static void xStreamBufferReset(int q){assert(q==1&&!g_session_enabled);resets++;}
static size_t xStreamBufferSend(int q,const void *p,size_t n,unsigned timeout){
 (void)p;(void)timeout;assert(q==1&&n==640&&g_session_enabled&&g_ws_ready&&g_phase==BOBER_LISTENING);sends++;return n;
}
static int i2s_channel_read(int ch,void *buf,size_t n,size_t *got,unsigned timeout){
 (void)ch;(void)timeout;assert(n==2560);stage=reads++;
 if(scenario==1||scenario==2){
  if(stage==1){assert(g_phase==BOBER_OFF&&!g_session_enabled&&!sends);assert(inits==1);assert(processes==(scenario==2));assert(deinits==(scenario==2));longjmp(done,1);}
 }else{
  if(stage==1){assert(!g_session_enabled&&!sends&&processes==1&&inits==1);if(scenario==3){bober_request_shake_wake();bober_request_shake_wake();}}
  if(stage==2){assert(g_session_enabled&&g_phase==BOBER_CONNECTING&&g_generation==2&&deinits==1&&resets==1&&!sends);}
  if(stage==3){assert(!sends&&processes==2);g_ws_ready=true;g_phase=BOBER_LISTENING;}
  if(stage==4){assert(sends==1&&processes==2);g_phase=BOBER_SPEAKING;bober_request_shake_wake();assert(!atomic_load(&shake_wake_requested));}
  if(stage==5){assert(sends==1&&processes==2);g_session_enabled=false;g_ws_ready=false;g_phase=BOBER_WAKE_WAIT;}
  if(stage==6){assert(sends==1&&processes==3&&inits==2&&!g_session_enabled);if(scenario==3)bober_request_shake_wake();}
  if(stage==7){assert(g_session_enabled&&g_phase==BOBER_CONNECTING&&g_generation==3&&deinits==2&&resets==2&&sends==1);power=true;return -1;}
 }
 int32_t *frames=buf;
 for(unsigned i=0;i<320;++i){frames[2*i]=1000*65536;frames[2*i+1]=0;}
 *got=n;return ESP_OK;
}
'''
main=r'''
int main(void){
 for(scenario=0;scenario<4;++scenario){
  g_session_enabled=g_ws_ready=g_ws_reconnect=power=healthy=false;g_phase=BOBER_WAKE_WAIT;g_generation=1;
  atomic_store(&shake_wake_requested,false);
  stage=reads=inits=deinits=processes=sends=resets=0;dc_prev_x=dc_prev_y=0;
  if(!setjmp(done))bober_mic_task(NULL);
 }
 puts("PASS: local-only waiting, word and shake/conversation/end/second wake, no uplink before connection, no wake during speech, fail-closed init/inference, power checkpoint");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(prefix+audio[audio.index('void bober_request_shake_wake'):audio.index('// External Atomic Voice Base')]+helpers+mic+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
