#ifndef FERRUM_NET_RELIABLE_ORDERED_CHANNEL_H
#define FERRUM_NET_RELIABLE_ORDERED_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file reliable_ordered_channel.h
 * @brief Reliable, ordered message delivery over an unreliable packet link.
 *
 * Ownership: The caller owns all buffers passed in and out.
 * Threading: Not thread-safe.
 */

typedef struct net_reliable_ordered_channel net_reliable_ordered_channel_t;

enum {
    NET_RELIABLE_ORDERED_OK = 0,
    NET_RELIABLE_ORDERED_EMPTY = 1,
    NET_RELIABLE_ORDERED_ERR_INVALID_ARGUMENT = -1,
    NET_RELIABLE_ORDERED_ERR_OUT_OF_MEMORY = -2,
    NET_RELIABLE_ORDERED_ERR_BUFFER_TOO_SMALL = -3,
    NET_RELIABLE_ORDERED_ERR_MALFORMED_PACKET = -4,
};

/**
 * @brief Initializes the channel.
 *
 * @param channel Non-null.
 * @param protocol_id Application protocol discriminator.
 * @param max_packet_size Maximum serialized packet size to emit.
 * @param max_reassembly_bytes Maximum in-flight reassembly buffer per message.
 * @param resend_timeout_ns Retransmit timeout.
 */
int net_reliable_ordered_channel_init(net_reliable_ordered_channel_t *channel,
                                     uint32_t protocol_id,
                                     uint32_t max_packet_size,
                                     uint32_t max_reassembly_bytes,
                                     uint64_t resend_timeout_ns);

/** @brief Releases all resources held by the channel. */
void net_reliable_ordered_channel_destroy(net_reliable_ordered_channel_t *channel);

/**
 * @brief Enqueues a message for reliable ordered delivery.
 *
 * @return NET_RELIABLE_ORDERED_OK on success.
 */
int net_reliable_ordered_channel_send(net_reliable_ordered_channel_t *channel, const void *data, size_t size);

/**
 * @brief Writes the next outgoing packet into @p out_packet.
 *
 * @param now_ns Current time.
 * @param out_packet Output buffer.
 * @param out_packet_capacity Capacity of @p out_packet.
 * @param out_packet_size Written size.
 *
 * @return NET_RELIABLE_ORDERED_OK if a packet was produced,
 *         NET_RELIABLE_ORDERED_EMPTY if nothing to send.
 */
int net_reliable_ordered_channel_next_packet(net_reliable_ordered_channel_t *channel,
                                            uint64_t now_ns,
                                            void *out_packet,
                                            size_t out_packet_capacity,
                                            size_t *out_packet_size);

/**
 * @brief Handles an incoming packet from the network.
 *
 * @param now_ns Current time.
 */
int net_reliable_ordered_channel_handle_packet(net_reliable_ordered_channel_t *channel,
                                              const void *packet,
                                              size_t packet_size,
                                              uint64_t now_ns);

/**
 * @brief Receives the next fully reassembled, in-order message.
 *
 * @return NET_RELIABLE_ORDERED_OK if a message was written,
 *         NET_RELIABLE_ORDERED_EMPTY if none available.
 */
int net_reliable_ordered_channel_receive(net_reliable_ordered_channel_t *channel,
                                        void *out_message,
                                        size_t out_message_capacity,
                                        size_t *out_message_size);

#ifdef __cplusplus
}
#endif

#endif
