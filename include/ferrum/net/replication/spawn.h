#ifndef FERRUM_NET_REPLICATION_SPAWN_H
#define FERRUM_NET_REPLICATION_SPAWN_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/vec3_mm.h"

/** @file
 * @brief SPAWN message: server -> client (reliable by resend).
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NET_REPL_SPAWN_PAYLOAD_SIZE 20u

typedef struct net_repl_spawn {
    uint32_t entity_id;
    uint16_t owner_client_id;
    uint16_t join_time_u16;
    net_repl_vec3_mm_t pos_mm;
} net_repl_spawn_t;

int net_repl_spawn_encode(const net_repl_spawn_t *msg, uint8_t *out_payload, size_t out_size);
int net_repl_spawn_decode(net_repl_spawn_t *msg, const uint8_t *payload, size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_SPAWN_H */
