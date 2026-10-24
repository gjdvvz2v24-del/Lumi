#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum { BOBER_BUTTON_NONE=0, BOBER_BUTTON_CLICK, BOBER_BUTTON_HOLD } bober_button_event_t;
typedef struct {
    bool raw_pressed, stable_pressed, hold_sent;
    uint32_t changed_at, pressed_at;
} bober_button_state_t;

bober_button_event_t bober_button_update(bober_button_state_t *state, bool pressed, uint32_t now);
void bober_picture_motion(uint32_t ms, bool speaking, bool listening, uint8_t level, int *dx, int *dy);

/* Raw BMI270 +/-8g units: 4096 counts/g, before orientation clipping. */
typedef enum { LUMI_REACTION_NONE=0, LUMI_REACTION_IMPACT, LUMI_REACTION_SHAKE } lumi_reaction_t;
typedef struct {
    int32_t gravity_q8[3], last_pulse[3];
    uint32_t sample_at, pulse_at, started_at, cooldown_at;
    unsigned pulses;
    bool initialized, cooling;
} lumi_motion_state_t;
lumi_reaction_t lumi_motion_update(lumi_motion_state_t *s,int16_t x,int16_t y,int16_t z,uint32_t now);
typedef struct { uint32_t frame, quiet_since; bool initialized; } lumi_slideshow_t;
uint32_t lumi_slideshow_update(lumi_slideshow_t *s,uint32_t now,bool voice,unsigned count);
