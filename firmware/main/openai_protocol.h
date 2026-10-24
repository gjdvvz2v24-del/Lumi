#pragma once
#include <stdint.h>
#include <stddef.h>
/* Linear 16 -> 24 kHz interpolation, continuous across arbitrary input chunks.
   Keep hardware/media at 16 kHz. Only OpenAI uplink uses 24 kHz. */
typedef struct { int32_t previous; unsigned phase; int initialized; } bober_resampler_t;
static inline size_t bober_resample(bober_resampler_t *s,const int16_t *in,size_t n,int16_t *out){
 size_t written=0;
 for(size_t i=0;i<n;i++){
  int32_t current=in[i];
  if(!s->initialized){s->previous=current;s->initialized=1;}
  while(s->phase<3){out[written++]=(int16_t)(s->previous+(current-s->previous)*(int32_t)s->phase/3);s->phase+=2;}
  s->phase-=3;s->previous=current;
 }
 return written;
}
