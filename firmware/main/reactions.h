#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "interaction.h"
void lumi_reaction_request(lumi_reaction_t event);
bool lumi_reaction_busy(void);
/* Called only by the I2S speaker owner. Audio never shares the TTS ring. */
size_t lumi_reaction_read(uint8_t *out,size_t capacity,bool enabled,uint32_t now);
