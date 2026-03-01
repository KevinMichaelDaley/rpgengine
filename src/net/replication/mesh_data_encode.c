/**
 * @file mesh_data_encode.c
 * @brief MESH_DATA chunk encode/decode — wire serialization.
 */
#include "ferrum/net/replication/mesh_data.h"

#include <string.h>

/* ── Wire helpers (big-endian) ─────────────────────────────────── */

static void write_u16_be_(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)(v >> 8); out[1] = (uint8_t)v;
}
static void write_u32_be_(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)(v >> 24); out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);  out[3] = (uint8_t)v;
}
static uint16_t read_u16_be_(const uint8_t *b) {
    return (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
}
static uint32_t read_u32_be_(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}

/* ── Encode ────────────────────────────────────────────────────── */

uint32_t net_repl_mesh_chunk_encode(const net_repl_mesh_chunk_t *chunk,
                                    uint8_t *out, size_t out_cap) {
    if (!chunk || !out) return 0;
    uint32_t need = NET_REPL_MESH_CHUNK_HEADER_SIZE + chunk->payload_size;
    if (out_cap < need) return 0;
    if (!chunk->payload && chunk->payload_size > 0) return 0;

    size_t o = 0;
    write_u16_be_(out + o, chunk->body_id);       o += 2;
    write_u16_be_(out + o, chunk->chunk_index);    o += 2;
    write_u16_be_(out + o, chunk->total_chunks);   o += 2;
    write_u32_be_(out + o, chunk->total_size);     o += 4;
    memcpy(out + o, chunk->payload, chunk->payload_size);

    return need;
}

/* ── Decode ────────────────────────────────────────────────────── */

int net_repl_mesh_chunk_decode(net_repl_mesh_chunk_t *chunk,
                               const uint8_t *data, size_t data_len) {
    if (!chunk || !data) return NET_REPL_ERR_INVALID;
    if (data_len < NET_REPL_MESH_CHUNK_HEADER_SIZE) return NET_REPL_ERR_SHORT;

    size_t o = 0;
    chunk->body_id       = read_u16_be_(data + o); o += 2;
    chunk->chunk_index   = read_u16_be_(data + o); o += 2;
    chunk->total_chunks  = read_u16_be_(data + o); o += 2;
    chunk->total_size    = read_u32_be_(data + o); o += 4;
    chunk->payload       = data + o;
    chunk->payload_size  = (uint16_t)(data_len - o);

    return NET_REPL_OK;
}
