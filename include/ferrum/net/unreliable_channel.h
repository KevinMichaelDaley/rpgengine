#ifndef FERRUM_NET_UNRELIABLE_CHANNEL_H
#define FERRUM_NET_UNRELIABLE_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief Fixed-size unreliable channel ring for high-rate packets.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status: operation succeeded. */
#define NET_UNRELIABLE_OK 0
/** Status: invalid arguments or channel not initialized. */
#define NET_UNRELIABLE_ERR_INVALID -1
/** Status: channel is full. */
#define NET_UNRELIABLE_ERR_FULL -2
/** Status: no packet available to receive. */
#define NET_UNRELIABLE_EMPTY 1

/** Fixed-size unreliable channel backed by a ring buffer. */
typedef struct net_unreliable_channel {
    uint8_t *payloads;
    size_t *sizes;
    size_t slot_count;
    size_t max_payload_size;
    size_t head;
    size_t tail;
    size_t count;
} net_unreliable_channel_t;

/**
 * @brief Initialize an unreliable channel.
 * @param channel Channel pointer (non-NULL).
 * @param slot_count Maximum number of queued packets.
 * @param max_payload_size Maximum payload size in bytes.
 */
void net_unreliable_channel_init(net_unreliable_channel_t *channel,
                                 size_t slot_count,
                                 size_t max_payload_size);

/**
 * @brief Destroy an unreliable channel and free its storage.
 * @param channel Channel pointer (NULL-safe).
 */
void net_unreliable_channel_destroy(net_unreliable_channel_t *channel);

/**
 * @brief Enqueue a payload onto the channel.
 * @param channel Channel pointer.
 * @param payload Payload bytes to copy (may be NULL if payload_size == 0).
 * @param payload_size Payload size in bytes.
 * @return NET_UNRELIABLE_OK on success or error code.
 */
int net_unreliable_channel_send(net_unreliable_channel_t *channel,
                                const void *payload,
                                size_t payload_size);

/**
 * @brief Dequeue the next payload from the channel.
 * @param channel Channel pointer.
 * @param out_payload Output buffer for payload bytes.
 * @param out_capacity Output buffer capacity in bytes.
 * @param out_size Output payload size.
 * @return NET_UNRELIABLE_OK on success, NET_UNRELIABLE_EMPTY if none available,
 *         or error code.
 */
int net_unreliable_channel_receive(net_unreliable_channel_t *channel,
                                   void *out_payload,
                                   size_t out_capacity,
                                   size_t *out_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_UNRELIABLE_CHANNEL_H */
