#ifndef FERRUM_NET_RELIABLE_CHANNEL_H
#define FERRUM_NET_RELIABLE_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief Reliable ordered channel for fixed-size message delivery.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status: operation succeeded. */
#define NET_RELIABLE_OK 0
/** Status: no message available. */
#define NET_RELIABLE_EMPTY 1
/** Status: invalid arguments or channel not initialized. */
#define NET_RELIABLE_ERR_INVALID -1
/** Status: channel window full/out of window. */
#define NET_RELIABLE_ERR_FULL -2
/** Status: requested sequence not found for resend. */
#define NET_RELIABLE_ERR_NOT_FOUND -3

/** Reliable ordered channel backed by fixed-size slots. */
typedef struct net_reliable_channel {
    uint8_t *payloads;
    size_t *sizes;
    uint16_t *sequences;
    uint8_t *occupied;
    size_t slot_count;
    size_t max_payload_size;
    size_t count;
    uint16_t next_send_sequence;
    uint16_t next_receive_sequence;
    uint8_t initialized;
} net_reliable_channel_t;

/**
 * @brief Initialize a reliable channel.
 * @param channel Channel pointer (non-NULL).
 * @param slot_count Maximum number of queued packets.
 * @param max_payload_size Maximum payload size in bytes.
 */
void net_reliable_channel_init(net_reliable_channel_t *channel,
                               size_t slot_count,
                               size_t max_payload_size);

/**
 * @brief Destroy a reliable channel and free its storage.
 * @param channel Channel pointer (NULL-safe).
 */
void net_reliable_channel_destroy(net_reliable_channel_t *channel);

/**
 * @brief Enqueue a payload with the next sequence number.
 * @param channel Channel pointer.
 * @param payload Payload bytes to copy.
 * @param payload_size Payload size in bytes.
 * @return NET_RELIABLE_OK on success or error code.
 */
int net_reliable_channel_send(net_reliable_channel_t *channel,
                              const void *payload,
                              size_t payload_size);

/**
 * @brief Enqueue a payload with an explicit sequence number.
 * @param channel Channel pointer.
 * @param sequence Sequence number for the payload.
 * @param payload Payload bytes to copy.
 * @param payload_size Payload size in bytes.
 * @return NET_RELIABLE_OK on success or error code.
 */
int net_reliable_channel_send_sequence(net_reliable_channel_t *channel,
                                       uint16_t sequence,
                                       const void *payload,
                                       size_t payload_size);

/**
 * @brief Mark a sequence for resend if still buffered.
 * @param channel Channel pointer.
 * @param sequence Sequence number to resend.
 * @return NET_RELIABLE_OK if found or error code.
 */
int net_reliable_channel_resend(net_reliable_channel_t *channel, uint16_t sequence);

/**
 * @brief Receive the next in-order payload.
 * @param channel Channel pointer.
 * @param out_payload Output buffer for payload bytes.
 * @param out_capacity Output buffer capacity in bytes.
 * @param out_size Output payload size.
 * @return NET_RELIABLE_OK on success, NET_RELIABLE_EMPTY if none available, or error code.
 */
int net_reliable_channel_receive(net_reliable_channel_t *channel,
                                 void *out_payload,
                                 size_t out_capacity,
                                 size_t *out_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_RELIABLE_CHANNEL_H */
