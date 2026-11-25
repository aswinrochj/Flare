/*
 * Inter-task messages and the handles that connect the tasks.
 * Tasks exchange data only through FreeRTOS primitives (DPR Section IV-B):
 *   sensor -> alarm      direct-to-task notification (state | flags << 8)
 *   sensor -> radio      8-deep event queue (ALERT / WARNING / FAULT)
 *   sensor -> heartbeat  1-deep "latest status" mailbox (xQueueOverwrite)
 *   diag   -> sensor     1-deep DHT22 result mailbox
 *   radio  -> heartbeat  1-deep beacon-time mailbox
 *   console/radio -> sensor  command queue (operator ACK, injections)
 *   all safety tasks -> health  "I am alive" event-group bits
 * The node_ctx_t below is filled once in app_main and only read afterwards.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "sn_proto.h"

/* health event-group bits */
#define ALIVE_SENSOR   BIT0
#define ALIVE_ALARM    BIT1
#define ALIVE_ALL      (ALIVE_SENSOR | ALIVE_ALARM)

/* debug event-group bits (failure-injection hooks, DPR Section VI (6)) */
#define DBG_HANG_SENSOR  BIT0
#define DBG_HANG_ALARM   BIT1
#define DBG_MUTE_RADIO   BIT2
#define DBG_FLOOD_LOG    BIT3

/* Latest node status, shared through the status mailbox. */
typedef struct {
    uint8_t  state;          /* sn_state_t */
    uint8_t  flags;
    uint8_t  cause;          /* cls_cause_t */
    uint16_t gas_x1000;
    uint16_t gas_raw;
    uint16_t flame_raw;
    int16_t  temp_x100;
    uint16_t vbat_mv;
    uint32_t uptime_s;
} node_status_t;

/* Event for the radio task. */
typedef struct {
    uint8_t       type;      /* SN_T_ALERT / WARNING / FAULT / HEARTBEAT */
    node_status_t st;
} radio_msg_t;

typedef enum {
    CMD_OPERATOR_ACK = 1,
    CMD_TOGGLE_FAULT,        /* arg = SN_F_*_FAULT bit           */
    CMD_GAS_OVERRIDE,        /* arg = Rs/R0 x 1000; 0 = off      */
    CMD_CALIBRATE,           /* capture R0 in clean air          */
} cmd_id_t;

typedef struct {
    cmd_id_t id;
    uint32_t arg;
} node_cmd_t;

typedef struct {
    bool    ok;
    int16_t temp_x100;
    int16_t hum_x10;
} dht_result_t;

typedef struct {
    TaskHandle_t       alarm_task;
    QueueHandle_t      radio_q;      /* radio_msg_t, 8 deep       */
    QueueHandle_t      status_mbx;   /* node_status_t, 1 deep     */
    QueueHandle_t      dht_mbx;      /* dht_result_t, 1 deep      */
    QueueHandle_t      beacon_mbx;   /* TickType_t, 1 deep        */
    QueueHandle_t      cmd_q;        /* node_cmd_t, 4 deep        */
    EventGroupHandle_t alive;
    EventGroupHandle_t debug;
    uint8_t            node_id;
    uint8_t            reset_cause;  /* sn_reset_t */
    uint8_t            reset_count;
    bool               selftest_fault;
    bool               radio_ok;
    dht_result_t       boot_dht;
} node_ctx_t;

/* Task entry points (parameter: node_ctx_t *) */
void radio_task(void *arg);
void heartbeat_task(void *arg);
void logger_task(void *arg);

/* radio.c: bring up SX1276 and (optionally) ESP-NOW before the tasks start */
bool radio_init(void);
