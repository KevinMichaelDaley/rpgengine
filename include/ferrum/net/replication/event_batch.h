#ifndef FERRUM_NET_REPLICATION_EVENT_BATCH_H
#define FERRUM_NET_REPLICATION_EVENT_BATCH_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"

/** @file
 * @brief EVENT message: server -> client (reliable by resend).
 *
 * This payload batches multiple variable-length events into a single stream message.
 *
 * Layout (big-endian):
 *  [0..1]  count (u16)
 *  [2..3]  server_tick (u16)
 *  then `count` entries:
 *    [0]     event_type (u8)
 *    [1]     reserved (u8)
 *    [2..9]  entity_key (u64)
 *    [10..11] payload_size (u16)
 *    [12..]  payload bytes
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NET_REPL_EVENT_SPAWN   1u
#define NET_REPL_EVENT_DESPAWN 2u
#define NET_REPL_EVENT_DEATH   3u

typedef struct net_repl_event_entry_view {
    uint8_t type;
    uint64_t entity_key;
    const uint8_t *payload;
    uint16_t payload_size;
} net_repl_event_entry_view_t;

int net_repl_event_batch_encode(uint16_t server_tick,
                               const net_repl_event_entry_view_t *entries,
                               uint16_t entry_count,
                               uint8_t *out_payload,
                               size_t out_capacity,
                               size_t *out_payload_size);

/** Decode an EVENT batch.
 *
 * Notes:
 * - `out_entries[i].payload` points into `payload`; the caller must keep `payload`
 *   alive while using decoded entry views.
 */
int net_repl_event_batch_decode(uint16_t *out_server_tick,
                               net_repl_event_entry_view_t *out_entries,
                               uint16_t out_entry_capacity,
                               uint16_t *out_entry_count,
                               const uint8_t *payload,
                               size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_EVENT_BATCH_H */
