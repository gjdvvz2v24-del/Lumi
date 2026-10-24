#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "interaction.h"
#include "reactions.h"
#include "power.h"

#define BOBER_RATE 16000
#define BOBER_PCM_CAPACITY (2u * 1024u * 1024u)
typedef enum { BOBER_OFF=0, BOBER_CONNECTING, BOBER_SPEAKING, BOBER_LISTENING, BOBER_WAKE_WAIT } bober_phase_t;
typedef struct {
    char ssid[33]; char password[65]; char openai_key[512]; char api_key[192]; char voice_id[96];
} bober_config_t;
extern bober_config_t g_cfg;
extern volatile bool g_wifi_ready;
extern volatile bool g_session_enabled;
extern volatile bool g_display_ready;
extern volatile uint8_t g_mic_level, g_speaker_level;
void bober_motion_task(void *arg);
bool bober_motion_latest(int16_t *x,int16_t *y,int16_t *z,uint32_t now);
extern volatile bool g_ws_ready;
extern volatile bool g_ws_reconnect;
extern volatile uint32_t g_generation;
extern volatile bober_phase_t g_phase;
extern volatile bool g_response_complete;
extern volatile int64_t g_last_pcm_ms;
extern volatile int64_t g_last_tts_begin_ms;
extern volatile int64_t g_last_play_ms;
extern StreamBufferHandle_t g_uplink;
extern SemaphoreHandle_t g_pcm_mutex;
extern uint8_t *g_pcm_ring;
extern size_t g_pcm_head, g_pcm_tail, g_pcm_count;
extern volatile uint32_t g_pcm_overruns;
int64_t bober_now_ms(void);
void bober_pcm_reset(void);
bool bober_pcm_push(const uint8_t *data, size_t len, uint32_t generation);
size_t bober_pcm_pop(uint8_t *out, size_t len);
size_t bober_pcm_available(void);
bool bober_wifi_start(void);
bool bober_wifi_suspend(void);
bool bober_audio_suspend(void);
bool bober_audio_init(void);
void bober_request_shake_wake(void);
void bober_mic_task(void *);
void bober_speaker_task(void *);
void bober_uplink_task(void *);
void bober_eleven_task(void *);
void bober_screen_task(void *);
void bober_button_task(void *);
