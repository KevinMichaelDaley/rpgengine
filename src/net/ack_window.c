#include "ferrum/net/ack_window.h"
#include <string.h>

static uint16_t sequence_diff(uint16_t newer, uint16_t older) {
    return (uint16_t)(newer - older);
}

static int sequence_more_recent(uint16_t a, uint16_t b) {
    return sequence_diff(a, b) < 32768u;
}

/** Shift the 256-bit bitfield left by `count` positions, then set bit 0. */
static void shift_left_and_set_lsb_(uint64_t bits[NET_ACK_WINDOW_WORDS], uint16_t count) {
    if (count >= NET_ACK_WINDOW_BITS) {
        /* Entire window is cleared; only bit 0 survives. */
        memset(bits, 0, sizeof(uint64_t) * NET_ACK_WINDOW_WORDS);
        bits[0] = 1u;
        return;
    }

    /* Shift in 64-bit-word granularity. */
    unsigned word_shift = count / 64u;
    unsigned bit_shift  = count % 64u;

    /* Move words upward to make room. */
    if (word_shift > 0u) {
        for (int w = (int)NET_ACK_WINDOW_WORDS - 1; w >= 0; --w) {
            int src = w - (int)word_shift;
            bits[w] = (src >= 0) ? bits[src] : 0u;
        }
    }

    /* Sub-word bit shift across all words. */
    if (bit_shift > 0u) {
        for (int w = (int)NET_ACK_WINDOW_WORDS - 1; w >= 0; --w) {
            uint64_t carry = (w > 0) ? (bits[w - 1] >> (64u - bit_shift)) : 0u;
            bits[w] = (bits[w] << bit_shift) | carry;
        }
    }

    /* Set bit 0 (the ack-1 position). */
    bits[0] |= 1u;
}

/** Test whether bit `pos` (1-based, 1..256) is set. */
static int test_bit_(const uint64_t bits[NET_ACK_WINDOW_WORDS], uint16_t pos) {
    if (pos == 0u || pos > NET_ACK_WINDOW_BITS) {
        return 0;
    }
    unsigned idx = (pos - 1u) / 64u;
    unsigned bit = (pos - 1u) % 64u;
    return (bits[idx] >> bit) & 1u;
}

/** Set bit `pos` (1-based, 1..256). */
static void set_bit_(uint64_t bits[NET_ACK_WINDOW_WORDS], uint16_t pos) {
    if (pos == 0u || pos > NET_ACK_WINDOW_BITS) {
        return;
    }
    unsigned idx = (pos - 1u) / 64u;
    unsigned bit = (pos - 1u) % 64u;
    bits[idx] |= ((uint64_t)1u << bit);
}

void net_ack_window_init(net_ack_window_t *window) {
    if (!window) {
        return;
    }
    window->ack = 0u;
    memset(window->ack_bits, 0, sizeof(window->ack_bits));
    window->initialized = 0u;
}

int net_ack_window_receive(net_ack_window_t *window, uint16_t sequence) {
    if (!window) {
        return NET_ACK_WINDOW_ERR_INVALID;
    }

    if (!window->initialized) {
        window->ack = sequence;
        memset(window->ack_bits, 0, sizeof(window->ack_bits));
        window->initialized = 1u;
        return NET_ACK_WINDOW_OK;
    }

    if (sequence == window->ack) {
        return NET_ACK_WINDOW_DUPLICATE;
    }

    if (sequence_more_recent(sequence, window->ack)) {
        uint16_t delta = sequence_diff(sequence, window->ack);
        shift_left_and_set_lsb_(window->ack_bits, delta);
        window->ack = sequence;
        return NET_ACK_WINDOW_OK;
    }

    uint16_t behind = sequence_diff(window->ack, sequence);
    if (behind == 0u) {
        return NET_ACK_WINDOW_DUPLICATE;
    }
    if (behind > NET_ACK_WINDOW_BITS) {
        return NET_ACK_WINDOW_OUT_OF_WINDOW;
    }

    if (test_bit_(window->ack_bits, behind)) {
        return NET_ACK_WINDOW_DUPLICATE;
    }
    set_bit_(window->ack_bits, behind);
    return NET_ACK_WINDOW_OK;
}

uint16_t net_ack_window_ack(const net_ack_window_t *window) {
    if (!window || !window->initialized) {
        return 0u;
    }
    return window->ack;
}

uint64_t net_ack_window_ack_bits_word(const net_ack_window_t *window, unsigned word_index) {
    if (!window || !window->initialized || word_index >= NET_ACK_WINDOW_WORDS) {
        return 0u;
    }
    return window->ack_bits[word_index];
}

void net_ack_window_ack_bits_all(const net_ack_window_t *window, uint64_t out[NET_ACK_WINDOW_WORDS]) {
    if (!window || !out) {
        if (out) {
            memset(out, 0, sizeof(uint64_t) * NET_ACK_WINDOW_WORDS);
        }
        return;
    }
    if (!window->initialized) {
        memset(out, 0, sizeof(uint64_t) * NET_ACK_WINDOW_WORDS);
        return;
    }
    memcpy(out, window->ack_bits, sizeof(uint64_t) * NET_ACK_WINDOW_WORDS);
}

int net_ack_window_is_acked(const net_ack_window_t *window, uint16_t sequence) {
    if (!window || !window->initialized) {
        return 0;
    }
    if (sequence == window->ack) {
        return 1;
    }
    uint16_t behind = sequence_diff(window->ack, sequence);
    if (behind == 0u || behind > NET_ACK_WINDOW_BITS) {
        return 0;
    }
    return test_bit_(window->ack_bits, behind);
}
