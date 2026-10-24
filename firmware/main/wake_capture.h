#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void lumi_capture_command_task(void *arg);
bool lumi_capture_feed(const int16_t *pcm,size_t count);
