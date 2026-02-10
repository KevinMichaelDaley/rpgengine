#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ferrum/net/replication/event_batch.h"

#define NET_REPL_EVENT_BATCH_HEADER_SIZE 4u
#define NET_REPL_EVENT_BATCH_ENTRY_HEADER_SIZE 12u

static void write_u64_be(uint8_t *out, uint64_t value) {
    out[0] = (uint8_t)((value >> 56) & 0xFFu);
    out[1] = (uint8_t)((value >> 48) & 0xFFu);
    out[2] = (uint8_t)((value >> 40) & 0xFFu);
    out[3] = (uint8_t)((value >> 32) & 0xFFu);
    out[4] = (uint8_t)((value >> 24) & 0xFFu);
    out[5] = (uint8_t)((value >> 16) & 0xFFu);
    out[6] = (uint8_t)((value >> 8) & 0xFFu);
    out[7] = (uint8_t)(value & 0xFFu);
}

static void write_u16_be(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)((value >> 8) & 0xFFu);
    out[1] = (uint8_t)(value & 0xFFu);
}

static uint64_t read_u64_be(const uint8_t *bytes) {
    return ((uint64_t)bytes[0] << 56) |
           ((uint64_t)bytes[1] << 48) |
           ((uint64_t)bytes[2] << 40) |
           ((uint64_t)bytes[3] << 32) |
           ((uint64_t)bytes[4] << 24) |
           ((uint64_t)bytes[5] << 16) |
           ((uint64_t)bytes[6] << 8) |
           (uint64_t)bytes[7];
}

static uint16_t read_u16_be(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

int net_repl_event_batch_encode(uint16_t server_tick,
                               const net_repl_event_entry_view_t *entries,
                               uint16_t entry_count,
                               uint8_t *out_payload,
                               size_t out_capacity,
                               size_t *out_payload_size) {
    if (!entries || !out_payload || !out_payload_size) {
        return NET_REPL_ERR_INVALID;
    }

    size_t total = NET_REPL_EVENT_BATCH_HEADER_SIZE;
    for (uint16_t i = 0u; i < entry_count; ++i) {
        const net_repl_event_entry_view_t *e = &entries[i];
        if (!e->payload && e->payload_size != 0u) {
            return NET_REPL_ERR_INVALID;
        }
        total += NET_REPL_EVENT_BATCH_ENTRY_HEADER_SIZE + (size_t)e->payload_size;
    }
    if (total > out_capacity) {
        return NET_REPL_ERR_SHORT;
    }

    write_u16_be(out_payload + 0u, entry_count);
    write_u16_be(out_payload + 2u, server_tick);

    size_t off = NET_REPL_EVENT_BATCH_HEADER_SIZE;
    for (uint16_t i = 0u; i < entry_count; ++i) {
        const net_repl_event_entry_view_t *e = &entries[i];
        out_payload[off + 0u] = e->type;
        out_payload[off + 1u] = 0u;
        write_u64_be(out_payload + off + 2u, e->entity_key);
        write_u16_be(out_payload + off + 10u, e->payload_size);
        if (e->payload_size != 0u) {
            memcpy(out_payload + off + NET_REPL_EVENT_BATCH_ENTRY_HEADER_SIZE, e->payload, e->payload_size);
        }
        off += NET_REPL_EVENT_BATCH_ENTRY_HEADER_SIZE + (size_t)e->payload_size;
    }

    *out_payload_size = total;
    return NET_REPL_OK;
}

int net_repl_event_batch_decode(uint16_t *out_server_tick,
                               net_repl_event_entry_view_t *out_entries,
                               uint16_t out_entry_capacity,
                               uint16_t *out_entry_count,
                               const uint8_t *payload,
                               size_t payload_size) {
    if (!out_server_tick || !out_entries || !out_entry_count || !payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (payload_size < (size_t)NET_REPL_EVENT_BATCH_HEADER_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    const uint16_t count = read_u16_be(payload + 0u);
    const uint16_t server_tick = read_u16_be(payload + 2u);
    if (count > out_entry_capacity) {
        return NET_REPL_ERR_SHORT;
    }

    size_t off = NET_REPL_EVENT_BATCH_HEADER_SIZE;
    for (uint16_t i = 0u; i < count; ++i) {
        if (off + NET_REPL_EVENT_BATCH_ENTRY_HEADER_SIZE > payload_size) {
            return NET_REPL_ERR_SHORT;
        }

        net_repl_event_entry_view_t *e = &out_entries[i];
        e->type = payload[off + 0u];
        e->entity_key = read_u64_be(payload + off + 2u);
        e->payload_size = read_u16_be(payload + off + 10u);

        const size_t need = NET_REPL_EVENT_BATCH_ENTRY_HEADER_SIZE + (size_t)e->payload_size;
        if (off + need > payload_size) {
            return NET_REPL_ERR_SHORT;
        }

        e->payload = payload + off + NET_REPL_EVENT_BATCH_ENTRY_HEADER_SIZE;
        off += need;
    }

    *out_server_tick = server_tick;
    *out_entry_count = count;
    return NET_REPL_OK;
}
