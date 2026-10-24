#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

/* Shared LP5562 bus; after init, motion task is the only IMU reader. */
bool bober_imu_suspend(void);
bool bober_imu_init(i2c_master_bus_handle_t bus);
bool bober_imu_read(int16_t *x, int16_t *y, int16_t *z);
