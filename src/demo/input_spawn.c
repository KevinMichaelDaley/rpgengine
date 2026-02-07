#include <stddef.h>
#include <stdint.h>

#include "ferrum/demo/input_spawn.h"

static void write_u32_be_(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)((value >> 24) & 0xFFu);
    out[1] = (uint8_t)((value >> 16) & 0xFFu);
    out[2] = (uint8_t)((value >> 8) & 0xFFu);
    out[3] = (uint8_t)(value & 0xFFu);
}

static void write_u16_be_(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)((value >> 8) & 0xFFu);
    out[1] = (uint8_t)(value & 0xFFu);
}

static uint32_t read_u32_be_(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static uint16_t read_u16_be_(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

int demo_input_spawn_encode(const demo_input_spawn_t *msg, uint8_t *out, size_t out_size) {
    if (!msg || !out) {
        return NET_REPL_ERR_INVALID;
    }
    if (out_size < (size_t)DEMO_INPUT_SPAWN_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    write_u16_be_(out + 0u,  msg->event_id);
    write_u16_be_(out + 2u,  msg->half_x_mm);
    write_u16_be_(out + 4u,  msg->half_y_mm);
    write_u16_be_(out + 6u,  msg->half_z_mm);
    write_u32_be_(out + 8u,  msg->color_seed);
    return NET_REPL_OK;
}

int demo_input_spawn_decode(demo_input_spawn_t *msg, const uint8_t *payload, size_t size) {
    if (!msg || !payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (size < (size_t)DEMO_INPUT_SPAWN_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    msg->event_id   = read_u16_be_(payload + 0u);
    msg->half_x_mm  = read_u16_be_(payload + 2u);
    msg->half_y_mm  = read_u16_be_(payload + 4u);
    msg->half_z_mm  = read_u16_be_(payload + 6u);
    msg->color_seed = read_u32_be_(payload + 8u);
    return NET_REPL_OK;
}
