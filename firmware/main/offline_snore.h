#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
 bool offline,playing,tail;
 uint32_t last_start_ms,tail_at;
 size_t offset;
} lumi_snore_state_t;
/* Speaker-owner only. No network calls, timers or additional audio task. */
size_t lumi_snore_read(lumi_snore_state_t *state,const uint8_t *sound,size_t sound_bytes,
 uint8_t *out,size_t capacity,bool connected,bool allowed,uint32_t now);
bool lumi_snore_busy(const lumi_snore_state_t *state);
