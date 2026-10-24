#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Single owner: the microphone task. Input is continuous 16 kHz mono PCM16.
bool lumi_wake_init(void);
void lumi_wake_self_test(void);
void lumi_wake_deinit(void);
bool lumi_wake_reset(void);
bool lumi_wake_process(const int16_t *samples, size_t count);
bool lumi_wake_healthy(void);
unsigned lumi_wake_match(void); // 1: Hej Lumi, 2: Luuu-miiii
float lumi_wake_score_for(unsigned model);
float lumi_wake_score(void);
float lumi_wake_peak_score(void);
size_t lumi_wake_arena_used(void);
float lumi_wake_signal_rms(void);
float lumi_wake_signal_ratio(void);
bool lumi_wake_signal_ready(void);
#ifdef __cplusplus
}
#endif
