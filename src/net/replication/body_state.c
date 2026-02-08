/**
 * @file body_state.c
 * @brief BODY_STATE encode/decode (unreliable, per-tick).
 */

#include "ferrum/net/replication/body_state.h"
#include "ferrum/net/replication/quat_smallest3.h"

/* ── Wire helpers ──────────────────────────────────────────────── */

static void write_u32_be(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)(v >> 24); out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);  out[3] = (uint8_t)v;
}
static void write_u16_be(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)(v >> 8); out[1] = (uint8_t)v;
}
static void write_i32_be(uint8_t *out, int32_t v) {
    out[0] = (uint8_t)((uint32_t)v >> 24); out[1] = (uint8_t)((uint32_t)v >> 16);
    out[2] = (uint8_t)((uint32_t)v >> 8);  out[3] = (uint8_t)v;
}
static void write_i16_be(uint8_t *out, int16_t v) {
    write_u16_be(out, (uint16_t)v);
}
static uint32_t read_u32_be(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}
static uint16_t read_u16_be(const uint8_t *b) {
    return (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
}
static int32_t read_i32_be(const uint8_t *b) {
    return (int32_t)(((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
                     ((uint32_t)b[2] << 8)  | (uint32_t)b[3]);
}
static int16_t read_i16_be(const uint8_t *b) {
    return (int16_t)read_u16_be(b);
}

/* ── Encode / Decode ───────────────────────────────────────────── */

int net_repl_body_state_encode(const net_repl_body_state_t *msg,
                               uint8_t *out, size_t out_size) {
    if (!msg || !out) return NET_REPL_ERR_INVALID;
    if (out_size < NET_REPL_BODY_STATE_PAYLOAD_SIZE) return NET_REPL_ERR_SHORT;

    size_t o = 0;
    write_u16_be(out + o, msg->server_tick);     o += 2;
    write_u16_be(out + o, msg->body_id);         o += 2;
    write_i32_be(out + o, msg->pos_mm.x_mm);     o += 4;
    write_i32_be(out + o, msg->pos_mm.y_mm);     o += 4;
    write_i32_be(out + o, msg->pos_mm.z_mm);     o += 4;

    /* Smallest-three quaternion → 7 bytes. */
    net_repl_quat_s3_t s3;
    net_repl_quat_s3_pack(msg->rot_x, msg->rot_y, msg->rot_z, msg->rot_w, &s3);
    net_repl_quat_s3_write(&s3, out + o);        o += NET_REPL_QUAT_S3_WIRE_SIZE;

    write_i16_be(out + o, msg->vel_x_mm_s);     o += 2;
    write_i16_be(out + o, msg->vel_y_mm_s);     o += 2;
    write_i16_be(out + o, msg->vel_z_mm_s);     o += 2;

    write_i16_be(out + o, msg->ang_x_mrad_s);   o += 2;
    write_i16_be(out + o, msg->ang_y_mrad_s);   o += 2;
    write_i16_be(out + o, msg->ang_z_mrad_s);   o += 2;

    write_u32_be(out + o, msg->send_time_ms);    o += 4;
    out[o] = msg->flags;                         o += 1;

    return NET_REPL_OK;
}

int net_repl_body_state_decode(net_repl_body_state_t *msg,
                               const uint8_t *payload, size_t payload_size) {
    if (!msg || !payload) return NET_REPL_ERR_INVALID;
    if (payload_size < NET_REPL_BODY_STATE_PAYLOAD_SIZE) return NET_REPL_ERR_SHORT;

    size_t o = 0;
    msg->server_tick = read_u16_be(payload + o);   o += 2;
    msg->body_id     = read_u16_be(payload + o);   o += 2;
    msg->pos_mm.x_mm = read_i32_be(payload + o);   o += 4;
    msg->pos_mm.y_mm = read_i32_be(payload + o);   o += 4;
    msg->pos_mm.z_mm = read_i32_be(payload + o);   o += 4;

    /* Smallest-three quaternion ← 7 bytes. */
    net_repl_quat_s3_t s3;
    net_repl_quat_s3_read(payload + o, &s3);        o += NET_REPL_QUAT_S3_WIRE_SIZE;
    net_repl_quat_s3_unpack(&s3, &msg->rot_x, &msg->rot_y,
                            &msg->rot_z, &msg->rot_w);

    msg->vel_x_mm_s = read_i16_be(payload + o);    o += 2;
    msg->vel_y_mm_s = read_i16_be(payload + o);    o += 2;
    msg->vel_z_mm_s = read_i16_be(payload + o);    o += 2;

    msg->ang_x_mrad_s = read_i16_be(payload + o);  o += 2;
    msg->ang_y_mrad_s = read_i16_be(payload + o);  o += 2;
    msg->ang_z_mrad_s = read_i16_be(payload + o);  o += 2;

    msg->send_time_ms = read_u32_be(payload + o);   o += 4;
    msg->flags        = payload[o];                  o += 1;

    return NET_REPL_OK;
}
