#include "interaction.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
int main(void){
 lumi_slideshow_t s={0};
 assert(lumi_slideshow_update(&s,0,false,8)==0);
 assert(lumi_slideshow_update(&s,3000,false,8)==1);
 for(unsigned t=3100;t<10000;t+=20)assert(lumi_slideshow_update(&s,t,true,8)==1);
 assert(lumi_slideshow_update(&s,12000,false,8)==1);
 assert(lumi_slideshow_update(&s,13000,false,8)==2);
 for(unsigned i=0;i<6;i++)lumi_slideshow_update(&s,16000+i*3000,false,8);
 assert(s.frame==0);
 memset(&s,0,sizeof(s));lumi_slideshow_update(&s,UINT32_MAX-2000,false,8);
 assert(lumi_slideshow_update(&s,1000,false,8)==1);

 lumi_motion_state_t m={0};
 for(unsigned t=0;t<2000;t+=10)assert(!lumi_motion_update(&m,0,0,4096,t));
 /* Gentle 1g shaking and rotation stay silent. */
 for(unsigned t=2000;t<4000;t+=10)
  assert(!lumi_motion_update(&m,(int16_t)(4096*sin(t*.0314159)),0,4096,t));
 for(unsigned t=4000;t<6000;t+=10)
  assert(!lumi_motion_update(&m,(int16_t)(4096*sin(t*.003)),0,(int16_t)(4096*cos(t*.003)),t));
 /* One hard impact followed by rest is not a shake gesture. */
 memset(&m,0,sizeof(m));lumi_motion_update(&m,0,0,4096,0);
 assert(!lumi_motion_update(&m,0,0,30000,10));
 for(unsigned t=20;t<1000;t+=10)assert(!lumi_motion_update(&m,0,0,4096,t));
 /* Repeated strong reversals trigger once; continuing within cooldown cannot. */
 unsigned hits=0;
 for(unsigned t=1000;t<3000;t+=10)
  hits+=lumi_motion_update(&m,(int16_t)(13000*sin((t-1000)*.0314159265)),0,4096,t)==LUMI_REACTION_SHAKE;
 assert(hits==1);
 for(unsigned t=3000;t<6000;t+=10)assert(!lumi_motion_update(&m,0,0,4096,t));
 for(unsigned t=6000;t<6800;t+=10)
  hits+=lumi_motion_update(&m,(int16_t)(13000*sin((t-6000)*.0314159265)),0,4096,t)==LUMI_REACTION_SHAKE;
 assert(hits==2);
 /* Stale input only seeds gravity, even if the first sample is extreme. */
 assert(!lumi_motion_update(&m,30000,0,0,10000));
 /* A sequence cannot accumulate isolated peaks across long pauses. */
 memset(&m,0,sizeof(m));lumi_motion_update(&m,0,0,4096,0);
 for(unsigned t=10;t<2000;t+=10){
  int16_t x=t%500==10?(t<1000?15000:-15000):0;
  assert(!lumi_motion_update(&m,x,0,4096,t));
 }
 /* Timestamp wrap retains a valid strong gesture. */
 memset(&m,0,sizeof(m));hits=0;
 for(unsigned t=0;t<800;t+=10)
  hits+=lumi_motion_update(&m,(int16_t)(13000*sin(t*.0314159265)),0,4096,UINT32_MAX-200+t)==LUMI_REACTION_SHAKE;
 assert(hits==1);
 puts("PASS: slideshow; gentle motion/rotation/isolated impact rejected; strong reversals, cooldown, stale samples and timer wrap");
}
