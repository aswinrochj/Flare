/* Node decision logic. SPDX-License-Identifier: MIT */
#include "classifier.h"

#include <string.h>
#include "sn_proto.h"

static uint8_t sat_inc(uint8_t v) { return v == 0xFF ? v : (uint8_t)(v + 1); }

void cls_init(classifier_t *c, uint32_t now_ms, bool selftest_fault,
              bool temp_ok_at_boot, int16_t temp_x100)
{
    memset(c, 0, sizeof(*c));
    c->gas_last_change_ms = now_ms;
    c->flame_last_change_ms = now_ms;
    c->selftest_fault = selftest_fault;
    c->temp_valid = temp_ok_at_boot;
    c->temp_fault = !temp_ok_at_boot;
    c->temp_x100 = temp_x100;
    if (temp_ok_at_boot) {
        c->temp_hist[0] = temp_x100;
        c->temp_hist_ms[0] = now_ms;
        c->temp_hist_n = 1;
        c->temp_hist_head = 1;
    }
    /* A node that has not yet vouched for its readings is never NORMAL. */
    c->state = SN_ST_FAULT;
}

static void step_temperature(classifier_t *c, const cls_input_t *in)
{
    if (!in->temp_new) {
        return;
    }
    if (!in->temp_ok) {
        c->temp_fault = true;           /* FR-13 */
        return;
    }
    c->temp_fault = false;
    c->temp_valid = true;
    c->temp_x100 = in->temp_x100;

    /* Ring of the last 16 readings (about 30 s at 2 s). */
    c->temp_hist[c->temp_hist_head] = in->temp_x100;
    c->temp_hist_ms[c->temp_hist_head] = in->now_ms;
    c->temp_hist_head = (uint8_t)((c->temp_hist_head + 1) % CFG_TEMP_RISE_WINDOW);
    if (c->temp_hist_n < CFG_TEMP_RISE_WINDOW) {
        c->temp_hist_n++;
    }

    int32_t rise_per_min = 0;
    if (c->temp_hist_n >= CFG_TEMP_RISE_WINDOW / 2 + 2) { /* >= ~18 s of history */
        uint8_t oldest = c->temp_hist_n < CFG_TEMP_RISE_WINDOW ? 0 : c->temp_hist_head;
        uint32_t dt = in->now_ms - c->temp_hist_ms[oldest];
        if (dt > 0) {
            rise_per_min = (int32_t)(in->temp_x100 - c->temp_hist[oldest]) * 60000 / (int32_t)dt;
        }
    }

    if (in->temp_x100 >= CFG_TEMP_DANGER_X100) {
        c->temp_hazard = true;
        c->temp_cause = CAUSE_TEMP;
    } else if (rise_per_min >= CFG_TEMP_RISE_X100_PER_MIN) {
        c->temp_hazard = true;
        c->temp_cause = CAUSE_TEMP_RISE;
    } else if (in->temp_x100 < CFG_TEMP_CLEAR_X100) {
        c->temp_hazard = false;
    }
}

cls_output_t cls_step(classifier_t *c, const cls_input_t *in)
{
    const uint32_t now = in->now_ms;
    uint8_t prev_state = c->state;
    uint8_t prev_flags = c->flags;

    /* ---------- gas: plausibility first ---------- */
    bool open_now = !in->gas_ok || in->gas_pin_mv < CFG_GAS_OPEN_PIN_MV;
    if (open_now) {
        c->gas_open_cnt = sat_inc(c->gas_open_cnt);
        c->gas_open_clear_cnt = 0;
        if (c->gas_open_cnt >= CFG_CONFIRM_SAMPLES) {
            c->gas_open = true;                       /* FAULT within 0.3 s */
        }
    } else {
        c->gas_open_cnt = 0;
        c->gas_open_clear_cnt = sat_inc(c->gas_open_clear_cnt);
        if (c->gas_open_clear_cnt >= CFG_CONFIRM_SAMPLES) {
            c->gas_open = false;
        }
    }

    if (in->gas_ok && in->gas_raw != c->gas_last_raw) {
        c->gas_last_raw = in->gas_raw;
        c->gas_last_change_ms = now;
    }
    bool gas_stuck = (now - c->gas_last_change_ms) >= CFG_STUCK_MS;

    bool railed_now = in->gas_ok && (in->gas_raw >= CFG_GAS_RAIL_RAW || in->gas_ao_mv >= CFG_GAS_RAIL_AO_MV);
    c->gas_rail_cnt = railed_now ? sat_inc(c->gas_rail_cnt) : 0;
    bool gas_rail = c->gas_rail_cnt >= CFG_CONFIRM_SAMPLES;

    bool gas_fault = c->gas_open || (gas_stuck && !gas_rail) || (in->injected_faults & SN_F_GAS_FAULT);

    /* ---------- gas: classification ---------- */
    bool gas_danger = false;
    if (!gas_fault && !railed_now) {
        c->gas_danger_cnt = (in->gas_x1000 <= CFG_GAS_DANGER_X1000) ? sat_inc(c->gas_danger_cnt) : 0;
        gas_danger = c->gas_danger_cnt >= CFG_CONFIRM_SAMPLES;

        if (in->gas_x1000 <= CFG_GAS_WARN_X1000) {
            c->gas_warn_cnt = sat_inc(c->gas_warn_cnt);
            if (c->gas_warn_cnt >= CFG_CONFIRM_SAMPLES) {
                c->gas_warn = true;
            }
        } else {
            c->gas_warn_cnt = 0;
            if (in->gas_x1000 >= CFG_GAS_CLEAR_X1000) {
                c->gas_warn = false;              /* hysteresis */
            }
        }
    } else {
        c->gas_danger_cnt = 0;
        c->gas_warn_cnt = 0;
        if (gas_fault) {
            c->gas_warn = false;
        }
    }

    /* ---------- flame ---------- */
    if (in->flame_ok && in->flame_raw != c->flame_last_raw) {
        c->flame_last_raw = in->flame_raw;
        c->flame_last_change_ms = now;
    }
    bool flame_fault = !in->flame_ok || (now - c->flame_last_change_ms) >= CFG_STUCK_MS ||
                       (in->injected_faults & SN_F_FLAME_FAULT);
    bool flame_danger = false;
    if (!flame_fault) {
        c->flame_cnt = in->flame_active ? sat_inc(c->flame_cnt) : 0;
        flame_danger = c->flame_cnt >= CFG_FLAME_CONFIRM_SAMPLES;
    } else {
        c->flame_cnt = 0;
    }

    /* ---------- temperature ---------- */
    step_temperature(c, in);
    bool temp_fault = c->temp_fault || (in->injected_faults & SN_F_TEMP_FAULT);
    bool temp_danger = !temp_fault && c->temp_hazard;

    /* ---------- battery ---------- */
    if (in->vbat_mv > CFG_VBAT_ABSENT_MV && in->vbat_mv < CFG_VBAT_LOW_MV) {
        c->low_batt = true;
    } else if (in->vbat_mv >= CFG_VBAT_LOW_CLEAR_MV || in->vbat_mv <= CFG_VBAT_ABSENT_MV) {
        c->low_batt = false;
    }

    /* ---------- DANGER: latched (FR-04) ---------- */
    bool hazard = gas_rail || gas_danger || flame_danger || temp_danger;
    if (hazard) {
        if (!c->latched) {
            c->acked = false;
        }
        c->latched = true;
        c->last_hazard_ms = now;
        c->danger_cause = gas_rail ? CAUSE_GAS_RAIL : gas_danger ? CAUSE_GAS
                        : flame_danger ? CAUSE_FLAME : c->temp_cause;
    }
    if (c->latched && in->operator_ack) {
        c->acked = true;
    }
    if (c->latched && c->acked && !hazard && (now - c->last_hazard_ms) >= CFG_CLEAR_HOLD_MS) {
        c->latched = false;
    }

    /* ---------- WARNING with 10 s hold ---------- */
    bool warn_now = c->gas_warn || c->low_batt;
    if (warn_now) {
        c->last_warn_ms = now;
        c->warn_hold = true;
    } else if (c->warn_hold && (now - c->last_warn_ms) >= CFG_CLEAR_HOLD_MS) {
        c->warn_hold = false;
    }

    /* ---------- flags ---------- */
    uint8_t flags = 0;
    if (in->flame_active && !flame_fault) flags |= SN_F_FLAME;
    if (gas_fault)                         flags |= SN_F_GAS_FAULT;
    if (temp_fault)                        flags |= SN_F_TEMP_FAULT;
    if (flame_fault)                       flags |= SN_F_FLAME_FAULT;
    if (c->low_batt)                       flags |= SN_F_LOW_BATT;
    if (c->latched)                        flags |= SN_F_LATCHED;

    bool any_fault = gas_fault || temp_fault || flame_fault || c->selftest_fault;

    /* ---------- state: hazard over fault over warning (FR-06) ---------- */
    uint8_t cause = CAUSE_NONE;
    if (c->latched) {
        c->state = SN_ST_DANGER;
        cause = c->danger_cause;
    } else if (any_fault) {
        c->state = SN_ST_FAULT;
        cause = c->selftest_fault ? CAUSE_SELFTEST
              : c->gas_open ? CAUSE_GAS_OPEN
              : (in->injected_faults & (SN_F_GAS_FAULT | SN_F_FLAME_FAULT | SN_F_TEMP_FAULT)) ? CAUSE_INJECTED
              : gas_fault ? CAUSE_GAS_STUCK
              : flame_fault ? CAUSE_FLAME_STUCK
              : CAUSE_TEMP_SENSOR;
    } else if (c->warn_hold) {
        c->state = SN_ST_WARNING;
        cause = c->gas_warn ? CAUSE_GAS_WARN : c->low_batt ? CAUSE_LOW_BATT : CAUSE_GAS_WARN;
    } else {
        c->state = SN_ST_NORMAL;
    }
    c->flags = flags;

    cls_output_t out = {
        .state = c->state,
        .flags = flags,
        .cause = cause,
        .state_changed = c->state != prev_state,
        .flags_changed = flags != prev_flags,
        .temp_x100 = c->temp_x100,
    };
    return out;
}

const char *cls_cause_name(uint8_t cause)
{
    static const char *const names[CAUSE_COUNT] = {
        "none", "gas", "gas input railed", "flame", "temperature", "temperature rise",
        "gas warning", "low battery", "gas sensor open", "gas sensor stuck",
        "flame sensor stuck", "temperature sensor", "self-test", "injected fault",
    };
    return cause < CAUSE_COUNT ? names[cause] : "unknown";
}
