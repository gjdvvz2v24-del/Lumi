// Both engines receive every block. Either phrase starts the same session.
#include "wake_detector.h"
#include "wake_engines.h"
#include <algorithm>
static unsigned matched;
extern "C" bool lumi_wake_init(void) {
    matched=0;
    if(hej_wake_init() && lumiii_wake_init())return true;
    lumi_wake_deinit();return false;
}
extern "C" void lumi_wake_deinit(void){hej_wake_deinit();lumiii_wake_deinit();matched=0;}
extern "C" bool lumi_wake_reset(void){
    matched=0;
    bool a=hej_wake_reset(),b=lumiii_wake_reset();return a&&b;
}
extern "C" bool lumi_wake_process(const int16_t *pcm,size_t count){
    bool a=hej_wake_process(pcm,count),b=lumiii_wake_process(pcm,count);
    matched=a?1:b?2:0;
    return (a||b)&&lumi_wake_healthy();
}
extern "C" unsigned lumi_wake_match(void){return matched;}
extern "C" bool lumi_wake_healthy(void){return hej_wake_healthy()&&lumiii_wake_healthy();}
extern "C" float lumi_wake_score(void){return std::max(hej_wake_score(),lumiii_wake_score());}
extern "C" float lumi_wake_score_for(unsigned model){return model==1?hej_wake_score():model==2?lumiii_wake_score():0;}
extern "C" float lumi_wake_peak_score(void){return std::max(hej_wake_peak_score(),lumiii_wake_peak_score());}
extern "C" size_t lumi_wake_arena_used(void){return hej_wake_arena_used()+lumiii_wake_arena_used();}
extern "C" float lumi_wake_signal_rms(void){return hej_wake_signal_rms();}
extern "C" float lumi_wake_signal_ratio(void){return hej_wake_signal_ratio();}
extern "C" bool lumi_wake_signal_ready(void){return hej_wake_signal_ready();}
#ifdef ESP_PLATFORM

#include <cstring>
#include "esp_log.h"
extern const uint8_t hej_start[] asm("_binary_selftest_pcm_start");
extern const uint8_t hej_end[] asm("_binary_selftest_pcm_end");
extern const uint8_t lumiii_start[] asm("_binary_selftest_lumiii_pcm_start");
extern const uint8_t lumiii_end[] asm("_binary_selftest_lumiii_pcm_end");
extern "C" void lumi_wake_self_test(void){
 const uint8_t *starts[]={hej_start,lumiii_start},*ends[]={hej_end,lumiii_end};
 for(unsigned model=1;model<=2;model++){
  if(!lumi_wake_reset())return;
  bool heard=false;float peak=0;int16_t pcm[160];
  size_t size=ends[model-1]-starts[model-1];
  for(size_t offset=0;offset<size;offset+=sizeof(pcm)){
   size_t bytes=std::min(sizeof(pcm),size-offset);
   std::memcpy(pcm,starts[model-1]+offset,bytes);
   lumi_wake_process(pcm,bytes/2);
   bool hit=model==1?hej_wake_score()>.55f:lumiii_wake_score()>.95f;
   heard=heard||(hit&&lumi_wake_signal_ready());
   peak=std::max(peak,lumi_wake_score_for(model));
  }
  ESP_LOGI("wake","DUAL_SELF_TEST model=%u above_threshold=%d peak=%.3f healthy=%d",model,heard,(double)peak,lumi_wake_healthy());
 }
 lumi_wake_reset();
}
#endif
