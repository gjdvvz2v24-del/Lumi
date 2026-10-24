#pragma once
#include <stdbool.h>
#include <stdint.h>
#define BOBER_POWER_MIC (1u<<0)
#define BOBER_POWER_SPEAKER (1u<<1)
#define BOBER_POWER_NETWORK (1u<<2)
#define BOBER_POWER_UPLINK (1u<<3)
#define BOBER_POWER_SCREEN (1u<<4)
#define BOBER_POWER_MOTION (1u<<5)
bool bober_power_init(void);
bool bober_power_requested(void);
void bober_power_register(uint32_t bit);
void bober_power_unregister(uint32_t bit);
/* Only call at a resource-free boundary. Tasks remain parked until reboot. */
void bober_power_checkpoint(uint32_t bit);
void bober_power_sleep(void);
bool bober_power_woke(void);
