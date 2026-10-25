#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* PCM is streamed through a bounded FIFO, not held as a whole story. */
#define LUMI_TTS_MAX_AUDIO_BYTES (16000u*2u*300u)
#define LUMI_TTS_TOTAL_MS 360000
#define LUMI_TTS_IDLE_MS 30000
#define LUMI_PLAYBACK_STALL_MS 90000
static inline bool lumi_tts_playback_stalled(int64_t now,int64_t start,int64_t received,int64_t played){
 int64_t latest=start;
 if(received>latest)latest=received;
 if(played>latest)latest=played;
 return now>=latest&&now-latest>LUMI_PLAYBACK_STALL_MS;
}
