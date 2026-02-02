#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/spawn_batch.h"

#define NET_REPL_SPAWN_BATCH_HEADER_SIZE 4u
#define NET_REPL_SPAWN_BATCH_ENTRY_SIZE 18u

static void write_u32_be(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)((value >> 24) & 0xFFu);
    out[1] = (uint8_t)((value >> 16) & 0xFFu);
    out[2] = (uint8_t)((value >> 8) & 0xFFu);
    out[3] = (uint8_t)(value & 0xFFu);
}

static void write_u16_be(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)((value >> 8) & 0xFFu);
    out[1] = (uint8_t)(value & 0xFFu);
}

static void write_i32_be(uint8_t *out, int32_t value) {
    write_u32_be(out, (uint32_t)value);
}

static uint32_t read_u32_be(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static uint16_t read_u16_be(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

static int32_t read_i32_be(const uint8_t *bytes) {
    return (int32_t)read_u32_be(bytes);
}

int net_repl_spawn_batch_encode(uint16_t server_tick,
                               const net_repl_spawn_batch_entry_t *entries,
                               uint16_t entry_count,
                               uint8_t *out_payload,
                               size_t out_capacity,
                               size_t *out_payload_size) {
    if (!entries || !out_payload || !out_payload_size) {
        return NET_REPL_ERR_INVALID;
    }

    const size_t total = NET_REPL_SPAWN_BATCH_HEADER_SIZE + (size_t)entry_count * (size_t)NET_REPL_SPAWN_BATCH_ENTRY_SIZE;
    if (total > out_capacity) {
        return NET_REPL_ERR_SHORT;
    }

    write_u16_be(out_payload + 0, entry_count);
    write_u16_be(out_payload + 2, server_tick);

    size_t off = NET_REPL_SPAWN_BATCH_HEADER_SIZE;
    for (uint16_t i = 0u; i < entry_count; ++i) {
        const net_repl_spawn_batch_entry_t *e = &entries[i];
        write_u32_be(out_payload + off + 0u, e->entity_id);
        write_u16_be(out_payload + off + 4u, e->owner_client_id);
        write_i32_be(out_payload + off + 6u, e->pos_mm.x_mm);
        write_i32_be(out_payload + off + 10u, e->pos_mm.y_mm);
        write_i32_be(out_payload + off + 14u, e->pos_mm.z_mm);
        off += NET_REPL_SPAWN_BATCH_ENTRY_SIZE;
    }

    *out_payload_size = total;
    return NET_REPL_OK;
}

int net_repl_spawn_batch_decode(uint16_t *out_server_tick,
                               net_repl_spawn_batch_entry_t *out_entries,
                               uint16_t out_entry_capacity,
                               uint16_t *out_entry_count,
                               const uint8_t *payload,
                               size_t payload_size) {
    if (!out_server_tick || !out_entries || !out_entry_count || !payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (payload_size < (size_t)NET_REPL_SPAWN_BATCH_HEADER_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    const uint16_t count = read_u16_be(payload + 0);
    const uint16_t tick = read_u16_be(payload + 2);
    const size_t needed = NET_REPL_SPAWN_BATCH_HEADER_SIZE + (size_t)count * (size_t)NET_REPL_SPAWN_BATCH_ENTRY_SIZE;
    if (payload_size < needed) {
        return NET_REPL_ERR_SHORT;
    }
    if (count > out_entry_capacity) {
        return NET_REPL_ERR_SHORT;
    }

    size_t off = NET_REPL_SPAWN_BATCH_HEADER_SIZE;
    for (uint16_t i = 0u; i < count; ++i) {
        net_repl_spawn_batch_entry_t *e = &out_entries[i];
        e->entity_id = read_u32_be(payload + off + 0u);
        e->owner_client_id = read_u16_be(payload + off + 4u);
        e->pos_mm.x_mm = read_i32_be(payload + off + 6u);
        e->pos_mm.y_mm = read_i32_be(payload + off + 10u);
        e->pos_mm.z_mm = read_i32_be(payload + off + 14u);
        off += NET_REPL_SPAWN_BATCH_ENTRY_SIZE;
    }

    *out_entry_count = count;
    *out_server_tick = tick;
    return NET_REPL_OK;
}
