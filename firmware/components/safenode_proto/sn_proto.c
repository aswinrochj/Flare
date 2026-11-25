/* SAFE-NODE radio protocol encoder/decoder. SPDX-License-Identifier: MIT */
#include "sn_proto.h"

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

uint16_t sn_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t sn_frame_encode(const sn_frame_t *f, uint8_t out[SN_FRAME_LEN])
{
    uint32_t up = f->uptime_s > 0xFFFFFFu ? 0xFFFFFFu : f->uptime_s;
    out[0] = f->node_id;
    out[1] = f->type;
    put16(&out[2], f->seq);
    out[4] = f->state;
    out[5] = f->flags;
    put16(&out[6], f->gas_x1000);
    put16(&out[8], (uint16_t)f->temp_x100);
    put16(&out[10], f->vbat_mv);
    out[12] = (uint8_t)((f->reset_cause & 0x0F) | ((f->reset_count & 0x0F) << 4));
    out[13] = (uint8_t)up;
    out[14] = (uint8_t)(up >> 8);
    out[15] = (uint8_t)(up >> 16);
    put16(&out[16], f->auth);
    put16(&out[18], sn_crc16(out, 18));
    return SN_FRAME_LEN;
}

sn_err_t sn_frame_decode(const uint8_t *buf, size_t len, sn_frame_t *f)
{
    if (len != SN_FRAME_LEN) {
        return SN_ERR_LEN;
    }
    if (get16(&buf[18]) != sn_crc16(buf, 18)) {
        return SN_ERR_CRC;
    }
    f->node_id = buf[0];
    f->type = buf[1];
    f->seq = get16(&buf[2]);
    f->state = buf[4];
    f->flags = buf[5];
    f->gas_x1000 = get16(&buf[6]);
    f->temp_x100 = (int16_t)get16(&buf[8]);
    f->vbat_mv = get16(&buf[10]);
    f->reset_cause = buf[12] & 0x0F;
    f->reset_count = buf[12] >> 4;
    f->uptime_s = (uint32_t)buf[13] | ((uint32_t)buf[14] << 8) | ((uint32_t)buf[15] << 16);
    f->auth = get16(&buf[16]);
    if (f->node_id < 1 || f->node_id > SN_MAX_NODE_ID) {
        return SN_ERR_FIELD;
    }
    if (f->type < SN_T_HEARTBEAT || f->type > SN_T_FAULT) {
        return SN_ERR_FIELD;
    }
    if (f->state > SN_ST_FAULT) {
        return SN_ERR_FIELD;
    }
    return SN_OK;
}

size_t sn_ack_encode(uint8_t node_id, uint16_t seq, uint8_t out[SN_ACK_LEN])
{
    out[0] = node_id;
    out[1] = SN_T_ACK;
    put16(&out[2], seq);
    return SN_ACK_LEN;
}

sn_err_t sn_ack_decode(const uint8_t *buf, size_t len, sn_ack_t *a)
{
    if (len != SN_ACK_LEN) {
        return SN_ERR_LEN;
    }
    if (buf[1] != SN_T_ACK) {
        return SN_ERR_FIELD;
    }
    a->node_id = buf[0];
    a->type = buf[1];
    a->seq = get16(&buf[2]);
    return SN_OK;
}

size_t sn_beacon_encode(uint16_t cycle, uint8_t out[SN_BEACON_LEN])
{
    out[0] = SN_GATEWAY_ID;
    out[1] = SN_T_BEACON;
    put16(&out[2], cycle);
    return SN_BEACON_LEN;
}

sn_err_t sn_beacon_decode(const uint8_t *buf, size_t len, sn_beacon_t *b)
{
    if (len != SN_BEACON_LEN) {
        return SN_ERR_LEN;
    }
    if (buf[0] != SN_GATEWAY_ID || buf[1] != SN_T_BEACON) {
        return SN_ERR_FIELD;
    }
    b->cycle = get16(&buf[2]);
    return SN_OK;
}

uint8_t sn_peek_type(const uint8_t *buf, size_t len)
{
    return len >= 2 ? buf[1] : 0;
}

const char *sn_state_name(uint8_t state)
{
    switch (state) {
    case SN_ST_NORMAL:  return "NORMAL";
    case SN_ST_WARNING: return "WARNING";
    case SN_ST_DANGER:  return "DANGER";
    case SN_ST_FAULT:   return "FAULT";
    default:            return "UNKNOWN";
    }
}

const char *sn_type_name(uint8_t type)
{
    switch (type) {
    case SN_T_HEARTBEAT: return "HEARTBEAT";
    case SN_T_ALERT:     return "ALERT";
    case SN_T_WARNING:   return "WARNING";
    case SN_T_FAULT:     return "FAULT";
    case SN_T_ACK:       return "ACK";
    case SN_T_BEACON:    return "BEACON";
    default:             return "UNKNOWN";
    }
}
