#ifndef FERRUM_NET_REPLICATION_SPAWN_BATCH_H
#define FERRUM_NET_REPLICATION_SPAWN_BATCH_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/vec3_mm.h"

/** @file
 * @brief SPAWN_BATCH message: server -> client (reliable by resend).
 *
 * Used to efficiently deliver many SPAWN entries to late joiners.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct net_repl_spawn_batch_entry {
    uint32_t entity_id;
    uint16_t owner_client_id;
    net_repl_vec3_mm_t pos_mm;
} net_repl_spawn_batch_entry_t;

/** Encodes a spawn batch.
 *
 * Layout (big-endian):
 *  [0..1]  count (u16)
 *  [2..3]  server_tick (u16)
 *  then `count` entries:
 *    [0..3]  entity_id (u32)
 *    [4..5]  owner_client_id (u16)
 *    [6..17] pos_mm xyz (i32,i32,i32)
 */
int net_repl_spawn_batch_encode(uint16_t server_tick,
                               const net_repl_spawn_batch_entry_t *entries,
                               uint16_t entry_count,
                               uint8_t *out_payload,
                               size_t out_capacity,
                               size_t *out_payload_size);

int net_repl_spawn_batch_decode(uint16_t *out_server_tick,
                               net_repl_spawn_batch_entry_t *out_entries,
                               uint16_t out_entry_capacity,
                               uint16_t *out_entry_count,
                               const uint8_t *payload,
                               size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_SPAWN_BATCH_H */
