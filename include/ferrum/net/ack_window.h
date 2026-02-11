#ifndef FERRUM_NET_ACK_WINDOW_H
#define FERRUM_NET_ACK_WINDOW_H

#include <stdint.h>

/** @file
 * @brief ACK window tracking for reliable UDP protocol headers.
 *
 * Tracks up to 256 recent sequence numbers using a 256-bit bitfield
 * stored as four uint64_t words.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status: packet accepted and window updated. */
#define NET_ACK_WINDOW_OK 0
/** Status: invalid arguments. */
#define NET_ACK_WINDOW_ERR_INVALID -1
/** Status: packet already received. */
#define NET_ACK_WINDOW_DUPLICATE 1
/** Status: packet too old for window. */
#define NET_ACK_WINDOW_OUT_OF_WINDOW 2

/** Number of tracked bits in the ack window (excluding the ack itself). */
#define NET_ACK_WINDOW_BITS 256u

/** Number of uint64_t words used to store the ack bitfield. */
#define NET_ACK_WINDOW_WORDS 4u

/** Tracking window for ACK/ack_bits state.
 *  Tracks the latest received sequence (ack) plus the previous 256
 *  sequences via a 256-bit bitfield (4 × uint64_t). */
typedef struct net_ack_window {
    uint16_t ack;
    uint64_t ack_bits[NET_ACK_WINDOW_WORDS];
    uint8_t initialized;
} net_ack_window_t;

/**
 * @brief Initialize an ACK window.
 * @param window Window pointer (non-NULL).
 */
void net_ack_window_init(net_ack_window_t *window);

/**
 * @brief Receive a sequence number and update the ACK window.
 * @param window Window pointer.
 * @param sequence Sequence number received.
 * @return NET_ACK_WINDOW_OK on update, NET_ACK_WINDOW_DUPLICATE if already seen,
 *         NET_ACK_WINDOW_OUT_OF_WINDOW if too old, or NET_ACK_WINDOW_ERR_INVALID.
 */
int net_ack_window_receive(net_ack_window_t *window, uint16_t sequence);

/**
 * @brief Get the current ACK sequence number.
 * @param window Window pointer.
 * @return Latest ACK sequence number (0 if window is NULL or uninitialized).
 */
uint16_t net_ack_window_ack(const net_ack_window_t *window);

/**
 * @brief Get a word of the current ACK bitfield.
 * @param window Window pointer.
 * @param word_index Index of the 64-bit word (0..3).
 * @return ACK bitfield word (0 if window is NULL, uninitialized, or index out of range).
 */
uint64_t net_ack_window_ack_bits_word(const net_ack_window_t *window, unsigned word_index);

/**
 * @brief Copy all 4 words of the ack bitfield into caller-provided array.
 * @param window Window pointer.
 * @param out Array of NET_ACK_WINDOW_WORDS uint64_t values (non-NULL).
 */
void net_ack_window_ack_bits_all(const net_ack_window_t *window, uint64_t out[NET_ACK_WINDOW_WORDS]);

/**
 * @brief Test whether a specific sequence is acknowledged in the window.
 * @param window Window pointer.
 * @param sequence Sequence to test.
 * @return Non-zero if acknowledged, 0 otherwise.
 */
int net_ack_window_is_acked(const net_ack_window_t *window, uint16_t sequence);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_ACK_WINDOW_H */
