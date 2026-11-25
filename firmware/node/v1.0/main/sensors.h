/* Analog sensor drivers: MQ-2 gas, flame, battery. SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool     gas_ok;
    uint16_t gas_raw;        /* mean of 16 conversions          */
    uint32_t gas_pin_mv;     /* eFuse-calibrated mV at GPIO32   */
    uint32_t gas_ao_mv;      /* x 2.2 for the R1/R2 divider     */
    uint16_t gas_x1000;      /* Rs/R0 x 1000 (0xFFFF if open)   */
    bool     flame_ok;
    uint16_t flame_raw;
    bool     flame_active;
    uint16_t vbat_mv;
} sensor_sample_t;

esp_err_t sensors_init(void);
void      sensors_read(sensor_sample_t *s);

/* Clean-air baseline R0 (stored as (Rs/RL) of R0, x1000) in NVS. */
uint32_t  sensors_r0_x1000(void);
bool      sensors_is_commissioned(void);
/* Capture R0 from `n` samples taken now; the air must be clean. */
esp_err_t sensors_calibrate(int n);
/* Slow exponential tracking of the clean-air baseline; returns true if the
 * tracked baseline has drifted more than CFG_GAS_DRIFT_LIMIT_PCT from R0. */
bool      sensors_track_baseline(const sensor_sample_t *s, bool clean_air);
