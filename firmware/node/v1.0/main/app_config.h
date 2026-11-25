/*
 * SAFE-NODE node firmware v1.0: thresholds, timing, calibration and radio
 * settings. Everything that decides behaviour lives here so that it is
 * versioned with the code and each test log can cite it through the
 * firmware commit hash (DPR Section VIII-G).
 *
 * Pure C: no ESP-IDF headers, so the classifier can be unit-tested on a PC.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "sn_proto.h"

/* ---- Scheduling (DPR Table III) ------------------------------------- */
#define CFG_SAMPLE_PERIOD_MS        100u    /* sensor task period (FR-01)           */
#define CFG_HEALTH_PERIOD_MS        500u    /* health task: check alive bits        */
#define CFG_WDT_TIMEOUT_MS         3000u    /* task watchdog (FR-14); sdkconfig too */
#define CFG_DHT_PERIOD_MS          2000u    /* DHT22 no faster than 2 s (FR-02)     */
#define CFG_STATUS_PRINT_MS        1000u    /* console status line (AR-10)          */

/* ---- Gas channel: MQ-2 behind R1 = 12 k / R2 = 10 k (HR-01) -------- */
#define CFG_GAS_OVERSAMPLE            16u
#define CFG_GAS_DIV_NUM               22u   /* V_AO = V_pin x (R1 + R2) / R2 */
#define CFG_GAS_DIV_DEN               10u
#define CFG_SENSOR_SUPPLY_MV        5000u   /* MQ-2 module VCC (HR-02)        */

/* Rs/R0 thresholds, x1000. Lower ratio = more gas.
 * From the log-log fit of the MQ-2 LPG curve, log10(Rs/R0) = -0.47 log10(ppm) + 1.31
 * (datasheet [12]); LPG LEL ~2 % = 20 000 ppm:
 *   10 % LEL = 2 000 ppm -> Rs/R0 = 0.574 ;  20 % LEL = 4 000 ppm -> Rs/R0 = 0.414
 * Adjust at commissioning (Section VII, Phase 5). */
#define CFG_GAS_WARN_X1000           574u   /* WARNING at 10 % LEL (FR-03)    */
#define CFG_GAS_DANGER_X1000         414u   /* DANGER at 20 % LEL (FR-03)     */
#define CFG_GAS_CLEAR_X1000          620u   /* hysteresis: clear above this   */
#define CFG_GAS_CLEAN_AIR_RATIO_X100 983u   /* Rs/R0 in clean air (datasheet) */
#define CFG_GAS_R0_DEFAULT_X1000     915u   /* (Rs/RL) of R0 before commissioning */
#define CFG_GAS_DRIFT_LIMIT_PCT       30u   /* baseline drift -> maintenance  */

#define CFG_CONFIRM_SAMPLES            3u   /* gas WARNING / DANGER, rail     */
#define CFG_FLAME_CONFIRM_SAMPLES      2u   /* flame                          */

/* Plausibility (DPR Table V) */
#define CFG_GAS_OPEN_PIN_MV           50u   /* < 0.05 V at GPIO32 = open wire */
#define CFG_GAS_RAIL_RAW            4013u   /* >= 98 % of 4095 counts         */
#define CFG_GAS_RAIL_AO_MV          4900u   /* >= 98 % of the 5 V module rail */
#define CFG_STUCK_MS               60000u   /* zero variance for 60 s (FR-12) */

/* Flame module (IR, analog out on GPIO33). Polarity is set in Kconfig
 * (AR-9); the threshold is the same 1800 counts as firmware v0.1. */
#define CFG_FLAME_THRESHOLD_RAW     1800u

/* Temperature (DHT22) */
#define CFG_TEMP_DANGER_X100        5000    /* 50 deg C                       */
#define CFG_TEMP_CLEAR_X100         4500
#define CFG_TEMP_RISE_X100_PER_MIN   800    /* 8 deg C / min                  */
#define CFG_TEMP_RISE_WINDOW         16u    /* 16 x 2 s = 30 s look-back      */

/* Battery (GPIO35 through 100 k / 100 k) */
#define CFG_VBAT_DIV                   2u
#define CFG_VBAT_LOW_MV             3400u   /* FR-16                          */
#define CFG_VBAT_LOW_CLEAR_MV       3500u
#define CFG_VBAT_ABSENT_MV          1000u   /* below this: no cell fitted     */

/* State machine */
#define CFG_CLEAR_HOLD_MS          10000u   /* hazard clear for 10 s          */

/* ---- Radio (Technical Documentation Table XI) ------------------------- */
#define CFG_LORA_FREQ_HZ       SN_LORA_FREQ_HZ
#define CFG_LORA_BW_HZ         SN_LORA_BW_HZ
#define CFG_LORA_CR_DENOM      SN_LORA_CR_DENOM
#define CFG_LORA_PREAMBLE      SN_LORA_PREAMBLE
#define CFG_LORA_TX_DBM               14
#define CFG_LORA_SYNC_WORD     SN_LORA_SYNC_WORD
#define CFG_HB_PERIOD_MS       SN_HB_PERIOD_MS  /* heartbeat (FR-20)              */
#define CFG_SLOT_MS            SN_SLOT_MS       /* slot = node ID x 150 ms        */
#define CFG_ACK_TIMEOUT_MS           200u
#define CFG_ALERT_RETRIES              3u
#define CFG_BACKOFF_MIN_MS            20u
#define CFG_BACKOFF_MAX_MS            80u
#define CFG_TX_TIMEOUT_MS            500u
#define CFG_TELEMETRY_QUEUE_LEN        8u   /* DPR Section IV-B               */
