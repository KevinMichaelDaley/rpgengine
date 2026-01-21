#ifndef FERRUM_NET_ACK_WINDOW_H
#define FERRUM_NET_ACK_WINDOW_H

#include <stdint.h>

/** @file
 * @brief ACK window tracking for reliable UDP protocol headers.
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

/** Tracking window for ACK/ack_bits state. */
typedef struct net_ack_window {
    uint16_t ack;
    uint32_t ack_bits;
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
 * @brief Get the current ACK bitfield.
 * @param window Window pointer.
 * @return ACK bitfield (0 if window is NULL or uninitialized).
 */
uint32_t net_ack_window_ack_bits(const net_ack_window_t *window);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_ACK_WINDOW_H */
