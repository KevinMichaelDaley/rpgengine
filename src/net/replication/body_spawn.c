/**
 * @file body_spawn.c
 * @brief BODY_SPAWN encode/decode (reliable, sent once per body).
 */

#include "ferrum/net/replication/body_spawn.h"
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
    write_u32_be(out, (uint32_t)v);
}
static uint32_t read_u32_be(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}
static uint16_t read_u16_be(const uint8_t *b) {
    return (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
}
static int32_t read_i32_be(const uint8_t *b) {
    return (int32_t)read_u32_be(b);
}

/* ── Encode / Decode ───────────────────────────────────────────── */

int net_repl_body_spawn_encode(const net_repl_body_spawn_t *msg,
                               uint8_t *out, size_t out_size) {
    if (!msg || !out) return NET_REPL_ERR_INVALID;
    if (out_size < NET_REPL_BODY_SPAWN_PAYLOAD_SIZE) return NET_REPL_ERR_SHORT;

    size_t o = 0;
    write_u16_be(out + o, msg->body_id);        o += 2;
    out[o++] = msg->flags;
    out[o++] = msg->shape_type;
    write_u32_be(out + o, msg->color_seed);     o += 4;
    write_i32_be(out + o, msg->pos_mm.x_mm);    o += 4;
    write_i32_be(out + o, msg->pos_mm.y_mm);    o += 4;
    write_i32_be(out + o, msg->pos_mm.z_mm);    o += 4;

    /* Smallest-three quaternion → 7 bytes. */
    net_repl_quat_s3_t s3;
    net_repl_quat_s3_pack(msg->rot_x, msg->rot_y, msg->rot_z, msg->rot_w, &s3);
    net_repl_quat_s3_write(&s3, out + o);       o += NET_REPL_QUAT_S3_WIRE_SIZE;

    write_u16_be(out + o, msg->half_x_f16);      o += 2;
    write_u16_be(out + o, msg->half_y_f16);       o += 2;
    write_u16_be(out + o, msg->half_z_f16);       o += 2;

    return NET_REPL_OK;
}

int net_repl_body_spawn_decode(net_repl_body_spawn_t *msg,
                               const uint8_t *payload, size_t payload_size) {
    if (!msg || !payload) return NET_REPL_ERR_INVALID;
    if (payload_size < NET_REPL_BODY_SPAWN_PAYLOAD_SIZE) return NET_REPL_ERR_SHORT;

    size_t o = 0;
    msg->body_id    = read_u16_be(payload + o);    o += 2;
    msg->flags      = payload[o++];
    msg->shape_type = payload[o++];
    msg->color_seed = read_u32_be(payload + o);    o += 4;
    msg->pos_mm.x_mm = read_i32_be(payload + o);  o += 4;
    msg->pos_mm.y_mm = read_i32_be(payload + o);  o += 4;
    msg->pos_mm.z_mm = read_i32_be(payload + o);  o += 4;

    /* Smallest-three quaternion ← 7 bytes. */
    net_repl_quat_s3_t s3;
    net_repl_quat_s3_read(payload + o, &s3);       o += NET_REPL_QUAT_S3_WIRE_SIZE;
    net_repl_quat_s3_unpack(&s3, &msg->rot_x, &msg->rot_y,
                            &msg->rot_z, &msg->rot_w);

    msg->half_x_f16 = read_u16_be(payload + o);     o += 2;
    msg->half_y_f16 = read_u16_be(payload + o);     o += 2;
    msg->half_z_f16 = read_u16_be(payload + o);     o += 2;

    return NET_REPL_OK;
}
