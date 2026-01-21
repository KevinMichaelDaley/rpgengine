#include "ferrum/net/ack_window.h"

static uint16_t sequence_diff(uint16_t newer, uint16_t older) {
    return (uint16_t)(newer - older);
}

static int sequence_more_recent(uint16_t a, uint16_t b) {
    return sequence_diff(a, b) < 32768u;
}

void net_ack_window_init(net_ack_window_t *window) {
    if (!window) {
        return;
    }
    window->ack = 0u;
    window->ack_bits = 0u;
    window->initialized = 0u;
}

int net_ack_window_receive(net_ack_window_t *window, uint16_t sequence) {
    if (!window) {
        return NET_ACK_WINDOW_ERR_INVALID;
    }

    if (!window->initialized) {
        window->ack = sequence;
        window->ack_bits = 0u;
        window->initialized = 1u;
        return NET_ACK_WINDOW_OK;
    }

    if (sequence == window->ack) {
        return NET_ACK_WINDOW_DUPLICATE;
    }

    if (sequence_more_recent(sequence, window->ack)) {
        uint16_t delta = sequence_diff(sequence, window->ack);
        if (delta >= 33u) {
            window->ack_bits = 0u;
        } else {
            window->ack_bits <<= delta;
        }
        window->ack_bits |= 1u;
        window->ack = sequence;
        return NET_ACK_WINDOW_OK;
    }

    uint16_t behind = sequence_diff(window->ack, sequence);
    if (behind == 0u) {
        return NET_ACK_WINDOW_DUPLICATE;
    }
    if (behind > 32u) {
        return NET_ACK_WINDOW_OUT_OF_WINDOW;
    }

    uint32_t bit = 1u << (behind - 1u);
    if (window->ack_bits & bit) {
        return NET_ACK_WINDOW_DUPLICATE;
    }
    window->ack_bits |= bit;
    return NET_ACK_WINDOW_OK;
}

uint16_t net_ack_window_ack(const net_ack_window_t *window) {
    if (!window || !window->initialized) {
        return 0u;
    }
    return window->ack;
}

uint32_t net_ack_window_ack_bits(const net_ack_window_t *window) {
    if (!window || !window->initialized) {
        return 0u;
    }
    return window->ack_bits;
}
