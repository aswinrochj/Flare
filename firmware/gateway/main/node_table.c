/* Gateway node table. SPDX-License-Identifier: MIT */
#include "node_table.h"

#include <string.h>

void nt_init(node_table_t *t)
{
    memset(t, 0, sizeof(*t));
}

uint32_t nt_offline_after_ms(uint8_t node_id)
{
    return NT_MISSED_HB * NT_HB_PERIOD_MS + (uint32_t)node_id * NT_SLOT_MS;
}

nt_result_t nt_on_frame(node_table_t *t, const sn_frame_t *f, uint32_t now_ms)
{
    nt_node_t *n = &t->node[f->node_id];
    bool was_offline = n->offline;
    bool dup = n->have_seq && f->seq == n->last_seq;

    n->known = true;
    n->offline = false;
    n->last_seen_ms = now_ms;   /* a duplicate still proves the node is alive */
    if (dup) {
        n->dup_count++;
        return NT_DUPLICATE;
    }
    n->last_seq = f->seq;
    n->have_seq = true;
    n->last = *f;
    n->rx_count++;
    if (f->type == SN_T_ALERT) {
        n->silenced = false;   /* a new alert sounds the siren again */
    }
    return was_offline ? NT_RECOVERED : NT_NEW;
}

uint32_t nt_check_liveness(node_table_t *t, uint32_t now_ms)
{
    uint32_t went_offline = 0;
    for (uint8_t id = 1; id <= SN_MAX_NODE_ID; id++) {
        nt_node_t *n = &t->node[id];
        if (n->known && !n->offline && (now_ms - n->last_seen_ms) > nt_offline_after_ms(id)) {
            n->offline = true;
            went_offline |= 1UL << id;
        }
    }
    return went_offline;
}

void nt_silence(node_table_t *t, uint8_t id)
{
    if (id >= 1 && id <= SN_MAX_NODE_ID) {
        t->node[id].silenced = true;
    }
}

bool nt_any_danger(const node_table_t *t)
{
    for (uint8_t id = 1; id <= SN_MAX_NODE_ID; id++) {
        const nt_node_t *n = &t->node[id];
        if (n->known && !n->offline && n->last.state == SN_ST_DANGER && !n->silenced) {
            return true;
        }
    }
    return false;
}

bool nt_any_unsafe(const node_table_t *t)
{
    for (uint8_t id = 1; id <= SN_MAX_NODE_ID; id++) {
        const nt_node_t *n = &t->node[id];
        if (n->known && (n->offline || n->last.state == SN_ST_FAULT)) {
            return true;
        }
    }
    return false;
}
