/* Gateway liveness and duplicate handling (FR-21, Tech. Doc. V-C). */
#include "minitest.h"
#include "node_table.h"

static node_table_t t;

static sn_frame_t hb(uint8_t id, uint16_t seq, uint8_t state)
{
    return (sn_frame_t){ .node_id = id, .type = SN_T_HEARTBEAT, .seq = seq, .state = state };
}

static void test_offline_bound(void)
{
    /* Table X: offline detection 9.15 s for node 1, under the 10 s objective */
    CHECK_EQ(nt_offline_after_ms(1), 9150);
    CHECK(nt_offline_after_ms(SN_MAX_NODE_ID) <= 12000);
}

static void test_goes_offline(void)
{
    nt_init(&t);
    sn_frame_t f = hb(1, 1, SN_ST_NORMAL);
    nt_on_frame(&t, &f, 1000);
    CHECK_EQ(nt_check_liveness(&t, 1000 + 9150), 0);
    CHECK_EQ(nt_check_liveness(&t, 1000 + 9151), 1u << 1);
    CHECK(t.node[1].offline);
    CHECK(nt_any_unsafe(&t));
    CHECK_EQ(nt_check_liveness(&t, 20000), 0);   /* reported once */

    f.seq = 2;
    CHECK_EQ(nt_on_frame(&t, &f, 21000), NT_RECOVERED);
    CHECK(!t.node[1].offline);
}

static void test_unknown_nodes_ignored(void)
{
    nt_init(&t);
    CHECK_EQ(nt_check_liveness(&t, 100000), 0);
    CHECK(!nt_any_unsafe(&t));
}

static void test_duplicates(void)
{
    nt_init(&t);
    sn_frame_t f = { .node_id = 4, .type = SN_T_ALERT, .seq = 77, .state = SN_ST_DANGER };
    CHECK_EQ(nt_on_frame(&t, &f, 0), NT_NEW);
    CHECK_EQ(nt_on_frame(&t, &f, 50), NT_DUPLICATE);   /* retry after a lost ACK */
    CHECK_EQ(t.node[4].dup_count, 1);
    CHECK(nt_any_danger(&t));
    nt_silence(&t, 4);
    CHECK(!nt_any_danger(&t));                  /* operator silenced the siren */
    f.seq = 78;
    CHECK_EQ(nt_on_frame(&t, &f, 100), NT_NEW);
    CHECK(nt_any_danger(&t));                   /* new alert sounds it again */
}

int main(void)
{
    RUN(test_offline_bound);
    RUN(test_goes_offline);
    RUN(test_unknown_nodes_ignored);
    RUN(test_duplicates);
    return MT_REPORT();
}
