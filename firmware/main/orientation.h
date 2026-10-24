#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t x, y, z;
    uint32_t last_sample, candidate_since;
    unsigned rotation, candidate;
    bool filtered, pending;
} bober_orientation_t;

/* BMI270 raw axes at +/-2 g (16384 counts/g). Returns clockwise quarter turns. */
unsigned bober_orientation_update(bober_orientation_t *s, int16_t x, int16_t y,
                                  int16_t z, uint32_t now_ms);
/* Drop stale samples after sleep/read failure; retain last visible rotation. */
void bober_orientation_pause(bober_orientation_t *s);

/* Convert BMI270 +/-8g samples to the orientation filter +/-2g scale. */
int16_t bober_orientation_scale(int16_t sample);
