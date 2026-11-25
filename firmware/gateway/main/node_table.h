/*
 * Gateway node table and liveness rule (Technical Documentation V-C, FR-21).
 * A node with no valid frame for 3 heartbeat periods plus its slot
 * (9 s + id x 0.15 s) is OFFLINE and shown as unsafe. A bad-CRC frame counts
 * as missing. A duplicate sequence number is acknowledged but not forwarded.
 *
 * Pure C, unit-tested on a PC (firmware/test/test_node_table.c).
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sn_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NT_HB_PERIOD_MS   SN_HB_PERIOD_MS
#define NT_SLOT_MS        SN_SLOT_MS
#define NT_MISSED_HB         3u

typedef struct {
    bool     known;       /* at least one valid frame since boot */
    bool     offline;
    uint32_t last_seen_ms;
    uint16_t last_seq;
    bool     have_seq;
    sn_frame_t last;      /* last valid frame                    */
    uint32_t rx_count;
    uint32_t dup_count;
    bool     silenced;    /* operator acknowledged this DANGER at the control room */
} nt_node_t;

typedef struct {
    nt_node_t node[SN_MAX_NODE_ID + 1];   /* index = node id; 0 unused */
} node_table_t;

typedef enum {
    NT_NEW,         /* forward to host                               */
    NT_DUPLICATE,   /* ACK again but do not forward (V-C)            */
    NT_RECOVERED,   /* first frame after OFFLINE: forward + RECOVERY */
} nt_result_t;

void        nt_init(node_table_t *t);
nt_result_t nt_on_frame(node_table_t *t, const sn_frame_t *f, uint32_t now_ms);
uint32_t    nt_offline_after_ms(uint8_t node_id);
/* Marks newly silent nodes OFFLINE. Returns a bitmask (bit n = node n)
 * of nodes that went OFFLINE during this call. */
uint32_t    nt_check_liveness(node_table_t *t, uint32_t now_ms);
/* Operator acknowledged node `id` at the control room: silence its siren
 * until the node raises a new ALERT. */
void        nt_silence(node_table_t *t, uint8_t id);
/* true if any online node is in DANGER and not silenced (siren on). */
bool        nt_any_danger(const node_table_t *t);
/* true if any known node is FAULT or OFFLINE (fault tone). */
bool        nt_any_unsafe(const node_table_t *t);

#ifdef __cplusplus
}
#endif
