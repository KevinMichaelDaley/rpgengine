/**
 * @file body_state_batch.h
 * @brief BODY_STATE_BATCH message: server -> client (unreliable).
 *
 * Packs multiple BODY_STATE payloads into a single message to reduce
 * per-body packet overhead.  Each entry is a raw 40-byte BODY_STATE
 * payload (same encoding as the individual message).
 *
 * Wire format:
 *   [count:u16 LE][body_state_0:40 bytes][body_state_1:40 bytes]...
 *
 * Maximum entries per batch is limited by the RUDP payload budget:
 *   (464 - 2) / 40 = 11 entries.
 */
#ifndef FERRUM_NET_REPLICATION_BODY_STATE_BATCH_H
#define FERRUM_NET_REPLICATION_BODY_STATE_BATCH_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/body_state.h"
#include "ferrum/net/replication/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum body states per batch (fits in one RUDP unreliable packet). */
#define NET_REPL_BODY_STATE_BATCH_MAX 11u

/** Maximum wire size: 2 + 11 * 40 = 442 bytes. */
#define NET_REPL_BODY_STATE_BATCH_MAX_SIZE \
    (2u + NET_REPL_BODY_STATE_BATCH_MAX * NET_REPL_BODY_STATE_PAYLOAD_SIZE)

/**
 * @brief Encode a batch of pre-encoded body state payloads.
 *
 * @param entries    Array of 40-byte encoded body state payloads.
 * @param count      Number of entries (1..NET_REPL_BODY_STATE_BATCH_MAX).
 * @param out        Output buffer.
 * @param out_size   Output buffer capacity.
 * @param out_len    Actual bytes written on success.
 * @return NET_REPL_OK on success.
 */
int net_repl_body_state_batch_encode(
    const uint8_t entries[][NET_REPL_BODY_STATE_PAYLOAD_SIZE],
    uint16_t count,
    uint8_t *out, size_t out_size, size_t *out_len);

/**
 * @brief Decode count and validate a batch payload.
 *
 * Does not decode individual entries; caller iterates with
 * net_repl_body_state_decode() on each 40-byte slice.
 *
 * @param payload      Wire payload (after schema ID).
 * @param payload_size Payload length.
 * @param out_count    Number of entries in the batch.
 * @param out_entries  Pointer to first entry (40-byte aligned).
 * @return NET_REPL_OK on success.
 */
int net_repl_body_state_batch_decode(
    const uint8_t *payload, size_t payload_size,
    uint16_t *out_count, const uint8_t **out_entries);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_BODY_STATE_BATCH_H */
