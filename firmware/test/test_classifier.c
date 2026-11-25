/* Decision-logic tests: each case names the requirement it verifies. */
#include "minitest.h"
#include "classifier.h"
#include "sn_proto.h"

static classifier_t c;
static cls_input_t in;
static cls_output_t out;

/* Clean air, healthy sensors, mains power (no battery). */
static void reset(void)
{
    cls_init(&c, 0, false, true, 2800);
    in = (cls_input_t){
        .now_ms = 0, .gas_ok = true, .gas_raw = 600, .gas_pin_mv = 500, .gas_ao_mv = 1100,
        .gas_x1000 = 2500, .flame_ok = true, .flame_raw = 300, .flame_active = false,
        .temp_new = false, .temp_ok = true, .temp_x100 = 2800, .vbat_mv = 0,
    };
}

/* One 100 ms step; the raw values wobble by one count like a real ADC. */
static void step(void)
{
    in.now_ms += CFG_SAMPLE_PERIOD_MS;
    in.gas_raw ^= 1;
    in.flame_raw ^= 1;
    out = cls_step(&c, &in);
    in.operator_ack = false;
    in.temp_new = false;
}

static void steps(int n)
{
    for (int i = 0; i < n; i++) {
        step();
    }
}

static void test_boot_then_normal(void)
{
    reset();
    CHECK_EQ(c.state, SN_ST_FAULT);          /* never NORMAL before the first sample */
    step();
    CHECK_EQ(out.state, SN_ST_NORMAL);
}

static void test_danger_needs_three_samples(void) /* FR-03, PR-01 */
{
    reset();
    step();
    in.gas_x1000 = 400;
    step();
    CHECK(out.state != SN_ST_DANGER);
    step();
    CHECK(out.state != SN_ST_DANGER);
    step();                                   /* third confirming sample: 0.3 s */
    CHECK_EQ(out.state, SN_ST_DANGER);
    CHECK(out.flags & SN_F_LATCHED);
    CHECK_EQ(out.cause, CAUSE_GAS);
}

static void test_transient_filtered(void)
{
    reset();
    step();
    in.gas_x1000 = 400;
    steps(2);
    in.gas_x1000 = 2500;
    step();
    in.gas_x1000 = 400;
    steps(2);
    CHECK(out.state != SN_ST_DANGER);
}

static void test_danger_latch_needs_ack_and_clear(void) /* FR-04 */
{
    reset();
    step();
    in.gas_x1000 = 400;
    steps(3);
    CHECK_EQ(out.state, SN_ST_DANGER);
    in.gas_x1000 = 2500;
    steps(150);                                /* 15 s clear, no ACK */
    CHECK_EQ(out.state, SN_ST_DANGER);
    in.operator_ack = true;
    step();
    CHECK_EQ(out.state, SN_ST_NORMAL);         /* clear > 10 s and acknowledged */

    /* ACK while the hazard is still present: wait for 10 s of clear readings */
    reset();
    step();
    in.gas_x1000 = 400;
    steps(3);
    in.operator_ack = true;
    step();
    in.gas_x1000 = 2500;
    steps(99);
    CHECK_EQ(out.state, SN_ST_DANGER);
    steps(2);
    CHECK_EQ(out.state, SN_ST_NORMAL);
}

static void test_warning_and_hysteresis(void) /* FR-03 */
{
    reset();
    step();
    in.gas_x1000 = 560;
    steps(3);
    CHECK_EQ(out.state, SN_ST_WARNING);
    CHECK_EQ(out.cause, CAUSE_GAS_WARN);
    in.gas_x1000 = 600;                         /* inside the hysteresis band */
    steps(120);
    CHECK_EQ(out.state, SN_ST_WARNING);
    in.gas_x1000 = 700;                         /* above clear threshold */
    steps(99);
    CHECK_EQ(out.state, SN_ST_WARNING);         /* 10 s hold */
    steps(2);
    CHECK_EQ(out.state, SN_ST_NORMAL);
}

static void test_open_wire_fault(void) /* FR-11 */
{
    reset();
    step();
    in.gas_pin_mv = 0;
    in.gas_raw = 0;
    in.gas_x1000 = 0xFFFF;
    steps(2);
    CHECK(out.state != SN_ST_FAULT);           /* not yet confirmed (0.2 s) */
    step();
    CHECK_EQ(out.state, SN_ST_FAULT);           /* 0.3 s */
    CHECK(out.flags & SN_F_GAS_FAULT);
    CHECK_EQ(out.cause, CAUSE_GAS_OPEN);
    steps(100);
    CHECK(out.state != SN_ST_NORMAL);           /* never NORMAL */
}

static void test_railed_input_alarms(void) /* Table V: fail toward alarm */
{
    reset();
    step();
    in.gas_raw = 4095;
    in.gas_ao_mv = 5000;
    steps(3);
    CHECK_EQ(out.state, SN_ST_DANGER);
    CHECK_EQ(out.cause, CAUSE_GAS_RAIL);
}

static void test_stuck_sensor(void) /* FR-12 */
{
    reset();
    step();
    for (int i = 0; i < 590; i++) {             /* constant raw value */
        in.now_ms += CFG_SAMPLE_PERIOD_MS;
        out = cls_step(&c, &in);
    }
    CHECK(out.state != SN_ST_FAULT);
    for (int i = 0; i < 15; i++) {
        in.now_ms += CFG_SAMPLE_PERIOD_MS;
        out = cls_step(&c, &in);
    }
    CHECK_EQ(out.state, SN_ST_FAULT);
    CHECK(out.flags & SN_F_GAS_FAULT);
    CHECK(out.flags & SN_F_FLAME_FAULT);
}

static void test_flame_two_samples(void)
{
    reset();
    step();
    in.flame_active = true;
    step();
    CHECK(out.state != SN_ST_DANGER);
    step();
    CHECK_EQ(out.state, SN_ST_DANGER);
    CHECK_EQ(out.cause, CAUSE_FLAME);
    CHECK(out.flags & SN_F_FLAME);
}

static void test_hazard_over_fault(void) /* FR-06 */
{
    reset();
    step();
    in.injected_faults = SN_F_TEMP_FAULT;
    step();
    CHECK_EQ(out.state, SN_ST_FAULT);
    in.flame_active = true;
    steps(2);
    CHECK_EQ(out.state, SN_ST_DANGER);
    CHECK(out.flags & SN_F_TEMP_FAULT);
}

static void test_dht_failure(void) /* FR-13 */
{
    reset();
    step();
    in.temp_new = true;
    in.temp_ok = false;
    step();
    CHECK_EQ(out.state, SN_ST_FAULT);
    CHECK_EQ(out.cause, CAUSE_TEMP_SENSOR);
    in.temp_new = true;
    in.temp_ok = true;
    step();
    CHECK_EQ(out.state, SN_ST_NORMAL);
}

static void test_over_temperature(void)
{
    reset();
    step();
    in.temp_new = true;
    in.temp_x100 = 5100;
    step();
    CHECK_EQ(out.state, SN_ST_DANGER);
    CHECK_EQ(out.cause, CAUSE_TEMP);
}

static void test_temperature_rise(void)
{
    reset();
    int16_t t = 2800;
    for (int i = 0; i < 20; i++) {               /* +0.4 C every 2 s = 12 C/min */
        steps(19);
        in.temp_new = true;
        in.temp_x100 = t;
        step();
        t += 40;
    }
    CHECK_EQ(out.state, SN_ST_DANGER);
    CHECK_EQ(out.cause, CAUSE_TEMP_RISE);

    reset();
    t = 2800;
    for (int i = 0; i < 20; i++) {               /* +0.1 C every 2 s = 3 C/min: no alarm */
        steps(19);
        in.temp_new = true;
        in.temp_x100 = t;
        step();
        t += 10;
    }
    CHECK_EQ(out.state, SN_ST_NORMAL);
}

static void test_low_battery(void) /* FR-16 */
{
    reset();
    in.vbat_mv = 3700;
    step();
    CHECK_EQ(out.state, SN_ST_NORMAL);
    in.vbat_mv = 3350;
    step();
    CHECK_EQ(out.state, SN_ST_WARNING);
    CHECK(out.flags & SN_F_LOW_BATT);
    CHECK_EQ(out.cause, CAUSE_LOW_BATT);
}

static void test_selftest_failure_sticks(void) /* FR-10 */
{
    cls_init(&c, 0, true, true, 2800);
    in.now_ms = 0;
    steps(50);
    CHECK_EQ(out.state, SN_ST_FAULT);
    CHECK_EQ(out.cause, CAUSE_SELFTEST);
}

int main(void)
{
    RUN(test_boot_then_normal);
    RUN(test_danger_needs_three_samples);
    RUN(test_transient_filtered);
    RUN(test_danger_latch_needs_ack_and_clear);
    RUN(test_warning_and_hysteresis);
    RUN(test_open_wire_fault);
    RUN(test_railed_input_alarms);
    RUN(test_stuck_sensor);
    RUN(test_flame_two_samples);
    RUN(test_hazard_over_fault);
    RUN(test_dht_failure);
    RUN(test_over_temperature);
    RUN(test_temperature_rise);
    RUN(test_low_battery);
    RUN(test_selftest_failure_sticks);
    return MT_REPORT();
}
