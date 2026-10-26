#include "offline_snore.h"
#include <string.h>
size_t lumi_snore_read(lumi_snore_state_t *s,const uint8_t *sound,size_t bytes,
 uint8_t *out,size_t capacity,bool connected,bool allowed,uint32_t now){
 if(s->tail&&(uint32_t)(now-s->tail_at)>=180)s->tail=false;
 if(connected){s->offline=false;s->playing=false;s->offset=0;return 0;}
 if(!s->offline){s->offline=true;s->last_start_ms=now;s->playing=false;s->offset=0;}
 if(!allowed){s->playing=false;s->offset=0;s->last_start_ms=now;return 0;}
 if(!s->playing&&(uint32_t)(now-s->last_start_ms)>=5000){
  s->playing=true;s->offset=0;s->last_start_ms=now;
 }
 if(!s->playing||!sound||!out||!bytes)return 0;
 size_t n=bytes-s->offset;if(n>capacity)n=capacity;n&=~(size_t)1;
 if(n){memcpy(out,sound+s->offset,n);s->offset+=n;s->tail=true;s->tail_at=now;}
 if(s->offset==bytes){s->playing=false;s->offset=0;}
 return n;
}
bool lumi_snore_busy(const lumi_snore_state_t *s){return s->playing||s->tail;}
