#include <stddef.h>
#include <stdint.h>

#include "ferrum/demo/input_move.h"

static void write_u16_be_(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)((value >> 8) & 0xFFu);
    out[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t read_u16_be_(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

static int16_t read_i16_be_(const uint8_t *bytes) {
    return (int16_t)read_u16_be_(bytes);
}

int demo_input_move_encode(const demo_input_move_t *msg, uint8_t *out, size_t out_size) {
    if (!msg || !out) {
        return NET_REPL_ERR_INVALID;
    }
    if (out_size < (size_t)DEMO_INPUT_MOVE_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    write_u16_be_(out + 0u, msg->event_id);
    write_u16_be_(out + 2u, (uint16_t)msg->yaw_snorm16);
    write_u16_be_(out + 4u, (uint16_t)msg->pitch_snorm16);
    out[6] = msg->move_flags;
    out[7] = msg->action_flags;
    return NET_REPL_OK;
}

int demo_input_move_decode(demo_input_move_t *msg, const uint8_t *payload, size_t size) {
    if (!msg || !payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (size < (size_t)DEMO_INPUT_MOVE_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    msg->event_id       = read_u16_be_(payload + 0u);
    msg->yaw_snorm16    = read_i16_be_(payload + 2u);
    msg->pitch_snorm16  = read_i16_be_(payload + 4u);
    msg->move_flags     = payload[6];
    msg->action_flags   = payload[7];
    return NET_REPL_OK;
}
