/*
 * Event log: producers post to a queue (never blocking a safety task); the
 * low-priority logger task prints each event and keeps the last 32 in an NVS
 * ring so the reset cause and history survive a reboot (FR-15).
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef enum {
    EV_BOOT = 1,        /* a = reset cause, b = reset count     */
    EV_STATE,           /* a = new state,   b = cause           */
    EV_SELFTEST_FAIL,   /* a = check index                      */
    EV_TX_FAIL,         /* a = frame type,  b = seq             */
    EV_ESPNOW_SENT,     /* a = frame type,  b = seq             */
    EV_OPERATOR_ACK,
    EV_CALIBRATED,      /* b = R0 x1000 (low 16 bits)           */
    EV_DRIFT,           /* baseline drift beyond limit          */
    EV_INJECT,          /* a = what                             */
} ev_code_t;

typedef struct {
    uint32_t uptime_s;
    uint8_t  code;
    uint8_t  a;
    uint16_t b;
} ev_t;

esp_err_t eventlog_init(void);
void      eventlog_add(ev_code_t code, uint8_t a, uint16_t b);
/* Called by the logger task only. */
int       eventlog_take(ev_t *ev, uint32_t timeout_ms);
void      eventlog_persist(const ev_t *ev);
void      eventlog_dump(void);
const char *eventlog_name(uint8_t code);
