/**
 * @file stream_priority.c
 * @brief STREAM_PRIORITY encode/decode (reliable, server -> client), rpg-3ldk.
 */
#include "ferrum/net/replication/stream_priority.h"

static void write_u16_be(uint8_t *o, uint16_t v) { o[0]=(uint8_t)(v>>8); o[1]=(uint8_t)v; }
static void write_u32_be(uint8_t *o, uint32_t v) {
    o[0]=(uint8_t)(v>>24); o[1]=(uint8_t)(v>>16); o[2]=(uint8_t)(v>>8); o[3]=(uint8_t)v;
}
static void write_u64_be(uint8_t *o, uint64_t v) {
    write_u32_be(o, (uint32_t)(v >> 32)); write_u32_be(o + 4, (uint32_t)v);
}
static uint16_t read_u16_be(const uint8_t *b) { return (uint16_t)(((uint16_t)b[0]<<8)|b[1]); }
static uint32_t read_u32_be(const uint8_t *b) {
    return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|(uint32_t)b[3];
}
static uint64_t read_u64_be(const uint8_t *b) {
    return ((uint64_t)read_u32_be(b) << 32) | (uint64_t)read_u32_be(b + 4);
}

int net_repl_stream_priority_encode(const net_repl_stream_priority_t *msg,
                                    uint8_t *out, size_t out_size, size_t *written)
{
    if (msg == NULL || out == NULL) return NET_REPL_ERR_INVALID;
    uint16_t n = msg->count;
    if (n > NET_REPL_STREAM_PRIORITY_MAX) n = NET_REPL_STREAM_PRIORITY_MAX;
    size_t need = NET_REPL_STREAM_PRIORITY_HEADER_SIZE +
                  (size_t)n * NET_REPL_STREAM_PRIORITY_ENTRY_SIZE;
    if (out_size < need) return NET_REPL_ERR_SHORT;

    size_t o = 0;
    write_u16_be(out + o, n); o += 2;
    for (uint16_t i = 0; i < n; ++i) {
        write_u64_be(out + o, msg->entries[i].id); o += 8;
        write_u32_be(out + o, (uint32_t)msg->entries[i].priority); o += 4;
    }
    if (written != NULL) *written = o;
    return NET_REPL_OK;
}

int net_repl_stream_priority_decode(net_repl_stream_priority_t *msg,
                                    const uint8_t *payload, size_t payload_size)
{
    if (msg == NULL || payload == NULL) return NET_REPL_ERR_INVALID;
    if (payload_size < NET_REPL_STREAM_PRIORITY_HEADER_SIZE) return NET_REPL_ERR_SHORT;
    uint16_t n = read_u16_be(payload);
    if (n > NET_REPL_STREAM_PRIORITY_MAX) return NET_REPL_ERR_SHORT;
    size_t need = NET_REPL_STREAM_PRIORITY_HEADER_SIZE +
                  (size_t)n * NET_REPL_STREAM_PRIORITY_ENTRY_SIZE;
    if (payload_size < need) return NET_REPL_ERR_SHORT;

    msg->count = n;
    size_t o = NET_REPL_STREAM_PRIORITY_HEADER_SIZE;
    for (uint16_t i = 0; i < n; ++i) {
        msg->entries[i].id = read_u64_be(payload + o); o += 8;
        msg->entries[i].priority = (int32_t)read_u32_be(payload + o); o += 4;
    }
    return NET_REPL_OK;
}
