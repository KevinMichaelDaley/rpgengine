#ifndef FERRUM_NET_REPLICATION_STATE_CUBE_H
#define FERRUM_NET_REPLICATION_STATE_CUBE_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/quat_snorm16.h"
#include "ferrum/net/replication/vec3_mm.h"

/** @file
 * @brief STATE-CUBE message: server -> client (unreliable).
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NET_REPL_STATE_CUBE_PAYLOAD_SIZE 28u

typedef struct net_repl_state_cube {
    uint16_t server_tick;
    uint32_t entity_id;
    net_repl_vec3_mm_t pos_mm;
    net_repl_quat_snorm16_t rot_snorm16;
} net_repl_state_cube_t;

int net_repl_state_cube_encode(const net_repl_state_cube_t *msg, uint8_t *out_payload, size_t out_size);
int net_repl_state_cube_decode(net_repl_state_cube_t *msg, const uint8_t *payload, size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_STATE_CUBE_H */
