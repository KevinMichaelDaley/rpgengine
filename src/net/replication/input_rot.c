#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/input_rot.h"

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

static int16_t read_i16_be_(const uint8_t *bytes) {
    return (int16_t)read_u16_be_(bytes);
}

int net_repl_input_rot_encode(const net_repl_input_rot_t *msg, uint8_t *out_payload, size_t out_size) {
    if (!msg || !out_payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (out_size < (size_t)NET_REPL_INPUT_ROT_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    write_u32_be_(out_payload + 0u, msg->entity_id);
    write_u32_be_(out_payload + 4u, msg->event_id);

    write_u16_be_(out_payload + 8u, (uint16_t)msg->axis_x_snorm16);
    write_u16_be_(out_payload + 10u, (uint16_t)msg->axis_y_snorm16);
    write_u16_be_(out_payload + 12u, (uint16_t)msg->axis_z_snorm16);

    write_u16_be_(out_payload + 14u, msg->speed_millirad_per_s);
    return NET_REPL_OK;
}

int net_repl_input_rot_decode(net_repl_input_rot_t *msg, const uint8_t *payload, size_t payload_size) {
    if (!msg || !payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (payload_size < (size_t)NET_REPL_INPUT_ROT_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    msg->entity_id = read_u32_be_(payload + 0u);
    msg->event_id = read_u32_be_(payload + 4u);

    msg->axis_x_snorm16 = read_i16_be_(payload + 8u);
    msg->axis_y_snorm16 = read_i16_be_(payload + 10u);
    msg->axis_z_snorm16 = read_i16_be_(payload + 12u);

    msg->speed_millirad_per_s = read_u16_be_(payload + 14u);
    return NET_REPL_OK;
}
