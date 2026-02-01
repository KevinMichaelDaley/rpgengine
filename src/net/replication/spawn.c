#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/spawn.h"

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

int net_repl_spawn_encode(const net_repl_spawn_t *msg, uint8_t *out_payload, size_t out_size) {
    if (!msg || !out_payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (out_size < (size_t)NET_REPL_SPAWN_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    write_u32_be(out_payload + 0, msg->entity_id);
    write_u16_be(out_payload + 4, msg->owner_client_id);
    write_u16_be(out_payload + 6, msg->join_time_u16);

    write_i32_be(out_payload + 8, msg->pos_mm.x_mm);
    write_i32_be(out_payload + 12, msg->pos_mm.y_mm);
    write_i32_be(out_payload + 16, msg->pos_mm.z_mm);

    return NET_REPL_OK;
}

int net_repl_spawn_decode(net_repl_spawn_t *msg, const uint8_t *payload, size_t payload_size) {
    if (!msg || !payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (payload_size < (size_t)NET_REPL_SPAWN_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    msg->entity_id = read_u32_be(payload + 0);
    msg->owner_client_id = read_u16_be(payload + 4);
    msg->join_time_u16 = read_u16_be(payload + 6);
    msg->pos_mm.x_mm = read_i32_be(payload + 8);
    msg->pos_mm.y_mm = read_i32_be(payload + 12);
    msg->pos_mm.z_mm = read_i32_be(payload + 16);

    return NET_REPL_OK;
}
