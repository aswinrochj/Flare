/* Protocol codec tests, including cross-check with the Python reference. */
#include <string.h>
#include "minitest.h"
#include "sn_proto.h"

#ifndef VECTORS_PATH
#define VECTORS_PATH "../../test/vectors/frames.txt"
#endif

static void test_crc_check_value(void)
{
    /* Standard check value of CRC-16/CCITT-FALSE */
    CHECK_EQ(sn_crc16((const uint8_t *)"123456789", 9), 0x29B1);
}

static void test_roundtrip(void)
{
    sn_frame_t f = { .node_id = 5, .type = SN_T_HEARTBEAT, .seq = 0xBEEF, .state = SN_ST_WARNING,
                     .flags = SN_F_LOW_BATT, .gas_x1000 = 600, .temp_x100 = -400, .vbat_mv = 3300,
                     .reset_cause = SN_RST_TASK_WDT, .reset_count = 18, .uptime_s = 0x1000000, .auth = 0 };
    uint8_t buf[SN_FRAME_LEN];
    CHECK_EQ(sn_frame_encode(&f, buf), SN_FRAME_LEN);
    sn_frame_t g;
    CHECK_EQ(sn_frame_decode(buf, sizeof buf, &g), SN_OK);
    CHECK_EQ(g.seq, 0xBEEF);
    CHECK_EQ(g.temp_x100, -400);
    CHECK_EQ(g.reset_cause, SN_RST_TASK_WDT);
    CHECK_EQ(g.reset_count, 18 % 16);
    CHECK_EQ(g.uptime_s, 0xFFFFFF);   /* saturated to 24 bits */
}

static void test_rejects(void)
{
    sn_frame_t f = { .node_id = 1, .type = SN_T_ALERT, .state = SN_ST_DANGER };
    uint8_t buf[SN_FRAME_LEN];
    sn_frame_encode(&f, buf);
    sn_frame_t g;
    CHECK_EQ(sn_frame_decode(buf, 19, &g), SN_ERR_LEN);
    buf[6] ^= 0x01;
    CHECK_EQ(sn_frame_decode(buf, sizeof buf, &g), SN_ERR_CRC);

    f.node_id = 21;                                /* out of range */
    sn_frame_encode(&f, buf);
    CHECK_EQ(sn_frame_decode(buf, sizeof buf, &g), SN_ERR_FIELD);
}

static void test_ack_beacon(void)
{
    uint8_t b[4];
    sn_ack_t a;
    sn_ack_encode(7, 300, b);
    CHECK_EQ(sn_ack_decode(b, 4, &a), SN_OK);
    CHECK_EQ(a.node_id, 7);
    CHECK_EQ(a.seq, 300);
    CHECK_EQ(sn_peek_type(b, 4), SN_T_ACK);
    sn_beacon_t bc;
    sn_beacon_encode(513, b);
    CHECK_EQ(sn_beacon_decode(b, 4, &bc), SN_OK);
    CHECK_EQ(bc.cycle, 513);
    CHECK_EQ(sn_ack_decode(b, 4, &a), SN_ERR_FIELD);
}

static int hexval(char c)
{
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

static void test_python_vectors(void)
{
    FILE *fp = fopen(VECTORS_PATH, "r");
    CHECK(fp != NULL);
    if (!fp) {
        return;
    }
    char line[256];
    int n = 0;
    while (fgets(line, sizeof line, fp)) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        unsigned id, type, seq, state, flags, gas, vbat, rc, rn;
        int temp;
        unsigned long up;
        char hex[64];
        if (sscanf(line, "%u %u %u %u %u %u %d %u %u %u %lu %63s", &id, &type, &seq, &state, &flags,
                   &gas, &temp, &vbat, &rc, &rn, &up, hex) != 12) {
            CHECK(0);
            continue;
        }
        sn_frame_t f = { (uint8_t)id, (uint8_t)type, (uint16_t)seq, (uint8_t)state, (uint8_t)flags,
                         (uint16_t)gas, (int16_t)temp, (uint16_t)vbat, (uint8_t)rc, (uint8_t)rn,
                         (uint32_t)up, 0 };
        uint8_t mine[SN_FRAME_LEN], ref[SN_FRAME_LEN];
        sn_frame_encode(&f, mine);
        for (unsigned i = 0; i < SN_FRAME_LEN; i++) {
            ref[i] = (uint8_t)(hexval(hex[2 * i]) << 4 | hexval(hex[2 * i + 1]));
        }
        CHECK(memcmp(mine, ref, SN_FRAME_LEN) == 0);
        sn_frame_t g;
        CHECK_EQ(sn_frame_decode(ref, SN_FRAME_LEN, &g), SN_OK);
        CHECK_EQ(g.temp_x100, temp);
        n++;
    }
    fclose(fp);
    CHECK(n >= 5);
}

int main(void)
{
    RUN(test_crc_check_value);
    RUN(test_roundtrip);
    RUN(test_rejects);
    RUN(test_ack_beacon);
    RUN(test_python_vectors);
    return MT_REPORT();
}
