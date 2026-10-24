#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Single owner: the microphone task. Input is continuous 16 kHz mono PCM16.
bool hej_wake_init(void);
void hej_wake_self_test(void);
void hej_wake_deinit(void);
bool hej_wake_reset(void);
bool hej_wake_process(const int16_t *samples, size_t count);
bool hej_wake_healthy(void);
float hej_wake_score(void);
float hej_wake_peak_score(void);
size_t hej_wake_arena_used(void);
float hej_wake_signal_rms(void);
float hej_wake_signal_ratio(void);
bool hej_wake_signal_ready(void);
#ifdef __cplusplus
}
#endif


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Single owner: the microphone task. Input is continuous 16 kHz mono PCM16.
bool lumiii_wake_init(void);
void lumiii_wake_self_test(void);
void lumiii_wake_deinit(void);
bool lumiii_wake_reset(void);
bool lumiii_wake_process(const int16_t *samples, size_t count);
bool lumiii_wake_healthy(void);
float lumiii_wake_score(void);
float lumiii_wake_peak_score(void);
size_t lumiii_wake_arena_used(void);
float lumiii_wake_signal_rms(void);
float lumiii_wake_signal_ratio(void);
bool lumiii_wake_signal_ready(void);
#ifdef __cplusplus
}
#endif
