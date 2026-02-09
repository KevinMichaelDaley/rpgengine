/**
 * Reliable UDP stream API and topic/channel abstraction.
 *
 * Provides an opaque stream context that reassembles reliable, ordered
 * messages from raw frames and exposes a simple pop interface per channel.
 * Also provides outbound message queuing and frame serialization.
 *
 * Concurrency: single-producer (RX thread) pushing frames, single-consumer
 * (gameplay/job side) popping messages, per stream. For multi-consumer
 * behavior, use `fr_topic_channel_t` as the consumption boundary.
 */
#ifndef FERRUM_NET_STREAM_H
#define FERRUM_NET_STREAM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** \file
 * Public API for reliable UDP stream reassembly and message delivery.
 */

/**
 * Callback invoked by fr_rudp_stream_flush_send to transmit a serialized
 * frame.  The frame contains the full wire data ready to send over UDP.
 *
 * @param user   Opaque user pointer passed to flush_send.
 * @param data   Frame bytes: [seq:u16 LE][chan:u16 LE][payload].
 * @param len    Total frame length.
 * @return 0 on success, non-zero on failure (flush stops on failure).
 */
typedef int (*fr_rudp_stream_sendto_fn)(void *user, const uint8_t *data, size_t len);

/**
 * Configuration for a reliable UDP stream.
 * Ownership: caller retains ownership of config memory; copied on create.
 */
typedef struct fr_rudp_stream_config_t {
    /** Number of reliable channels (>= 1). Defaults to 1 when 0. */
    uint32_t reliable_channels;
    /** Max queued packets per reliable channel. Defaults to 64 when 0. */
    uint32_t reliable_slot_count;
    /** Max payload size per packet in bytes. Defaults to 1024 when 0. */
    uint32_t max_payload_size;
    /** Optional topic channels array; when provided, decoded messages are pumped to topics. */
    struct fr_topic_channel **topics;
    /** Number of topic channels provided. */
    uint32_t num_topics;
} fr_rudp_stream_config_t;

/**
 * Opaque reliable UDP stream context.
 *
 * Lifetime: created via `fr_rudp_stream_create`, destroyed via
 * `fr_rudp_stream_destroy`. All functions are NULL-safe where documented.
 */
typedef struct fr_rudp_stream fr_rudp_stream_t;

/**
 * Create a reliable UDP stream context.
 *\param cfg Optional configuration; when NULL, defaults are used.
 *\return Stream pointer on success, or NULL on allocation/parameter failure.
 */
fr_rudp_stream_t *fr_rudp_stream_create(const fr_rudp_stream_config_t *cfg);

/**
 * Destroy a reliable UDP stream context and free internal buffers.
 *\param s Stream pointer; NULL-safe.
 */
void fr_rudp_stream_destroy(fr_rudp_stream_t *s);

/**
 * Push a raw frame into the stream reassembly path.
 *\param s Stream pointer (non-NULL).
 *\param data Pointer to frame bytes; first 2 bytes are little-endian sequence.
 *\param len Length of frame bytes (>= 2 for sequence + payload).
 *\return true on accept, false on invalid/duplicate/full.
 *
 * Notes:
 * - This minimal API assumes frames are per-channel and reliable. Channel 0 is
 *   used in the initial implementation. Future expansions may multiplex
 *   channel IDs in the frame header.
 */
bool fr_rudp_stream_push_frame(fr_rudp_stream_t *s, const uint8_t *data, size_t len);

/**
 * Pop the next in-order message for a channel.
 *\param s Stream pointer (non-NULL).
 *\param channel_id Channel index (0-based).
 *\param out Output buffer for payload bytes.
 *\param inout_len Input: capacity; Output: actual payload length on success.
 *\return true when a message is written to `out`; false if empty or invalid.
 */
bool fr_rudp_stream_pop(fr_rudp_stream_t *s, uint32_t channel_id, uint8_t *out, size_t *inout_len);

/**
 * Queue a message for outbound reliable delivery on a channel.
 *
 * The message is buffered internally with an auto-assigned sequence number.
 * Call fr_rudp_stream_flush_send() to serialize and transmit queued messages.
 *
 * @param s           Stream pointer (non-NULL).
 * @param channel_id  Channel index (0-based).
 * @param payload     Message payload bytes.
 * @param payload_len Payload length in bytes.
 * @return true on success, false if channel full or invalid.
 */
bool fr_rudp_stream_send(fr_rudp_stream_t *s, uint32_t channel_id,
                         const uint8_t *payload, size_t payload_len);

/**
 * Flush all queued outbound messages as serialized frames.
 *
 * For each queued message, constructs a frame [seq:u16 LE][chan:u16 LE][payload]
 * and invokes the sendto callback. Messages are removed from the outbound
 * buffer after successful transmission.
 *
 * @param s      Stream pointer (non-NULL).
 * @param sendto Callback to transmit each frame.
 * @param user   Opaque pointer forwarded to sendto.
 * @return Number of frames flushed, or 0 if nothing to send or error.
 */
uint32_t fr_rudp_stream_flush_send(fr_rudp_stream_t *s,
                                   fr_rudp_stream_sendto_fn sendto,
                                   void *user);

#endif /* FERRUM_NET_STREAM_H */
