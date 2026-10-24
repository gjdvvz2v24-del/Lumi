#include "wake_detector.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <random>
#include <algorithm>
int main(){
 if(!lumi_wake_init())return 2;
 int16_t samples[320];int failures=0;
 for(int kind=0;kind<10;kind++)for(int amplitude: {8,64,512,4096,20000}){
  if(!lumi_wake_reset())return 3;
  std::mt19937 rng(1234);std::uniform_real_distribution<float> noise(-1,1);
  int first=-1;float peak=0;float low=0;
  for(int frame=0;frame<500;frame++){
   for(int i=0;i<320;i++){
    int t=frame*320+i;float x=0;
    switch(kind){
     case 0: x=noise(rng);break;
     case 1: low=.95f*low+.05f*noise(rng);x=low;break;
     case 2:x=std::sin(2*3.141592653589793*50*t/16000);break;
     case 3:x=std::sin(2*3.141592653589793*1000*t/16000);break;
     case 4:x=1;break;
     case 5:x=t%16000<50?noise(rng):0;break;
     case 6:x=t<16000?noise(rng):0;break;
     case 7:x=std::sin(2*3.141592653589793*60*t/16000);break;
     case 8:x=std::sin(2*3.141592653589793*100*t/16000);break;
     case 9:x=std::sin(2*3.141592653589793*120*t/16000);break;
    }
    samples[i]=(int16_t)(x*amplitude);
   }
   if(lumi_wake_process(samples,320)&&first<0)first=frame*20;
   peak=std::max(peak,lumi_wake_score());
  }
  if(first>=0)failures++;
  printf("kind=%d amplitude=%d first_detection_ms=%d max_score=%.6f\n",kind,amplitude,first,peak);fflush(stdout);
 }
 lumi_wake_deinit();
 printf("False wakes: %d / 50 synthetic noise cases\n",failures);
 return failures?1:0;
}
