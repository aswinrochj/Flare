/*
 * Node decision logic (DPR Table V, SRS FR-03..FR-06, FR-11..FR-13, FR-16).
 * Pure C with no RTOS calls: it is driven by the sensor task every 100 ms on
 * the target and by firmware/test/test_classifier.c on a PC.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAUSE_NONE = 0,
    CAUSE_GAS,           /* gas above DANGER for 3 samples      */
    CAUSE_GAS_RAIL,      /* railed gas input (fail toward alarm)*/
    CAUSE_FLAME,
    CAUSE_TEMP,          /* T >= 50 deg C                       */
    CAUSE_TEMP_RISE,     /* rise >= 8 deg C / min               */
    CAUSE_GAS_WARN,      /* gas above WARNING                   */
    CAUSE_LOW_BATT,
    CAUSE_GAS_OPEN,      /* open wire on the gas input          */
    CAUSE_GAS_STUCK,
    CAUSE_FLAME_STUCK,
    CAUSE_TEMP_SENSOR,   /* DHT22 timeout / checksum / NaN      */
    CAUSE_SELFTEST,      /* a boot self-test check failed       */
    CAUSE_INJECTED,      /* fault injected from the debug UART  */
    CAUSE_COUNT
} cls_cause_t;

/* Inputs for one 100 ms step. */
typedef struct {
    uint32_t now_ms;

    bool     gas_ok;          /* ADC read succeeded                 */
    uint16_t gas_raw;         /* averaged counts, 0..4095           */
    uint32_t gas_pin_mv;      /* calibrated mV at GPIO32            */
    uint32_t gas_ao_mv;       /* mV at the module AO pin            */
    uint16_t gas_x1000;       /* Rs/R0 x 1000                       */

    bool     flame_ok;
    uint16_t flame_raw;
    bool     flame_active;    /* polarity already applied           */

    bool     temp_new;        /* a DHT22 read finished this step    */
    bool     temp_ok;         /* ... and it was valid               */
    int16_t  temp_x100;

    uint16_t vbat_mv;

    bool     operator_ack;    /* operator acknowledged DANGER       */
    uint8_t  injected_faults; /* SN_F_GAS_FAULT | SN_F_FLAME_FAULT | SN_F_TEMP_FAULT */
} cls_input_t;

typedef struct {
    uint8_t state;            /* sn_state_t                         */
    uint8_t flags;            /* SN_F_*                             */
    uint8_t cause;            /* cls_cause_t of the dominant reason */
    bool    state_changed;
    bool    flags_changed;
    int16_t temp_x100;        /* last valid temperature             */
} cls_output_t;

typedef struct {
    /* gas */
    uint8_t  gas_open_cnt, gas_open_clear_cnt;
    bool     gas_open;
    uint8_t  gas_rail_cnt;
    uint8_t  gas_danger_cnt;
    uint8_t  gas_warn_cnt;
    bool     gas_warn;
    uint16_t gas_last_raw;
    uint32_t gas_last_change_ms;
    /* flame */
    uint8_t  flame_cnt;
    uint16_t flame_last_raw;
    uint32_t flame_last_change_ms;
    /* temperature */
    bool     temp_fault;
    bool     temp_valid;
    int16_t  temp_x100;
    int16_t  temp_hist[CFG_TEMP_RISE_WINDOW];
    uint32_t temp_hist_ms[CFG_TEMP_RISE_WINDOW];
    uint8_t  temp_hist_n, temp_hist_head;
    bool     temp_hazard;
    uint8_t  temp_cause;
    /* battery */
    bool     low_batt;
    /* state */
    bool     selftest_fault;
    bool     latched;
    bool     acked;
    uint32_t last_hazard_ms;
    uint32_t last_warn_ms;
    bool     warn_hold;
    uint8_t  danger_cause;
    uint8_t  state;
    uint8_t  flags;
} classifier_t;

/* selftest_fault: a boot check failed (stays FAULT until reboot).
 * temp_ok_at_boot: the boot DHT22 read succeeded; its value is temp_x100. */
void         cls_init(classifier_t *c, uint32_t now_ms, bool selftest_fault,
                      bool temp_ok_at_boot, int16_t temp_x100);
cls_output_t cls_step(classifier_t *c, const cls_input_t *in);
const char  *cls_cause_name(uint8_t cause);

#ifdef __cplusplus
}
#endif
