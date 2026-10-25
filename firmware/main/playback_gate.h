#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Default I2S TX DMA is 6 * 240 frames / 16 kHz = 90 ms.
   Last-play timestamp precedes one 20 ms write; keep 120 ms total guard.
   Producer completion means no more PCM can arrive, so no network-tail timer. */
static inline bool lumi_sol_mic_can_resume(bool complete,size_t buffered,bool reacting,
                                          int64_t now,int64_t last_play){
 return complete&&!buffered&&!reacting&&now>=last_play&&now-last_play>=120;
}
