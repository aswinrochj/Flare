/*
 * SAFE-NODE radio protocol (Technical Documentation, Section V).
 *
 * Plain C99 with no ESP-IDF dependency, so the same code is used by the node,
 * the gateway and the host-side unit tests (firmware/test/).
 *
 * All multi-byte fields are little-endian. The CRC is CRC-16/CCITT-FALSE
 * (poly 0x1021, init 0xFFFF, no reflection, no final XOR) over bytes 0..17.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SN_FRAME_LEN   20u   /* node frame, Table XII */
#define SN_ACK_LEN      4u   /* gateway ACK: node_id, type, seq */
#define SN_BEACON_LEN   4u   /* gateway beacon: id (0), type, cycle */

/* Radio parameters shared by node and gateway (Technical Documentation Table XI) */
#define SN_LORA_FREQ_HZ      866000000UL  /* inside 865-867 MHz (HR-04)  */
#define SN_LORA_BW_HZ           125000UL
#define SN_LORA_CR_DENOM             5u   /* 4/5                         */
#define SN_LORA_PREAMBLE             8u
#define SN_LORA_SYNC_WORD         0x12u
#define SN_HB_PERIOD_MS           3000u   /* heartbeat and beacon period */
#define SN_SLOT_MS                 150u   /* slot = node ID x 150 ms     */

#define SN_GATEWAY_ID   0u
#define SN_MAX_NODE_ID 20u

/* An ACK whose seq is SN_SEQ_OPERATOR_ACK is an operator acknowledgement
 * of a latched DANGER (forwarded from the dashboard). Project extension of
 * Table XIII; ordinary ACKs never use this value because the node skips it. */
#define SN_SEQ_OPERATOR_ACK 0xFFFFu

typedef enum {
    SN_T_HEARTBEAT = 0x01,
    SN_T_ALERT     = 0x02,
    SN_T_WARNING   = 0x03,
    SN_T_FAULT     = 0x04,
    SN_T_ACK       = 0x05,
    SN_T_BEACON    = 0x06,
} sn_type_t;

typedef enum {
    SN_ST_NORMAL  = 0,
    SN_ST_WARNING = 1,
    SN_ST_DANGER  = 2,
    SN_ST_FAULT   = 3,
} sn_state_t;

/* flags byte (byte 5) */
#define SN_F_FLAME        (1u << 0)
#define SN_F_GAS_FAULT    (1u << 1)
#define SN_F_TEMP_FAULT   (1u << 2)
#define SN_F_FLAME_FAULT  (1u << 3)
#define SN_F_LOW_BATT     (1u << 4)
#define SN_F_LATCHED      (1u << 5)
#define SN_F_VIA_ESPNOW   (1u << 6)

/* reset cause codes (byte 12, bits 0-3) */
typedef enum {
    SN_RST_POWERON  = 0,
    SN_RST_SW       = 1,
    SN_RST_PANIC    = 2,
    SN_RST_TASK_WDT = 3,
    SN_RST_INT_WDT  = 4,
    SN_RST_BROWNOUT = 5,
    SN_RST_DEEPSLEEP= 6,
    SN_RST_EXT      = 7,
    SN_RST_OTHER    = 15,
} sn_reset_t;

typedef struct {
    uint8_t  node_id;      /* 1..20; 0 = gateway */
    uint8_t  type;         /* sn_type_t */
    uint16_t seq;
    uint8_t  state;        /* sn_state_t */
    uint8_t  flags;        /* SN_F_* */
    uint16_t gas_x1000;    /* Rs/R0 x 1000 */
    int16_t  temp_x100;    /* deg C x 100 */
    uint16_t vbat_mv;
    uint8_t  reset_cause;  /* 0..15 */
    uint8_t  reset_count;  /* stored mod 16 */
    uint32_t uptime_s;     /* saturates at 0xFFFFFF */
    uint16_t auth;         /* reserved, 0 */
} sn_frame_t;

typedef struct {
    uint8_t  node_id;
    uint8_t  type;         /* type being acknowledged is implied; always SN_T_ACK on air */
    uint16_t seq;
} sn_ack_t;

typedef struct {
    uint16_t cycle;
} sn_beacon_t;

typedef enum {
    SN_OK = 0,
    SN_ERR_LEN = -1,
    SN_ERR_CRC = -2,
    SN_ERR_FIELD = -3,
} sn_err_t;

uint16_t sn_crc16(const uint8_t *data, size_t len);

/* Encode into out[SN_FRAME_LEN]; computes the CRC. Returns SN_FRAME_LEN. */
size_t   sn_frame_encode(const sn_frame_t *f, uint8_t out[SN_FRAME_LEN]);
/* Decode and check length, CRC and field ranges. */
sn_err_t sn_frame_decode(const uint8_t *buf, size_t len, sn_frame_t *f);

size_t   sn_ack_encode(uint8_t node_id, uint16_t seq, uint8_t out[SN_ACK_LEN]);
sn_err_t sn_ack_decode(const uint8_t *buf, size_t len, sn_ack_t *a);

size_t   sn_beacon_encode(uint16_t cycle, uint8_t out[SN_BEACON_LEN]);
sn_err_t sn_beacon_decode(const uint8_t *buf, size_t len, sn_beacon_t *b);

/* Peek at the type byte of any received buffer (0 if too short). */
uint8_t  sn_peek_type(const uint8_t *buf, size_t len);

const char *sn_state_name(uint8_t state);
const char *sn_type_name(uint8_t type);

#ifdef __cplusplus
}
#endif
