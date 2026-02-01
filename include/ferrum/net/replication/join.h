#ifndef FERRUM_NET_REPLICATION_JOIN_H
#define FERRUM_NET_REPLICATION_JOIN_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"

/** @file
 * @brief JOIN message: client -> server (reliable by resend).
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NET_REPL_JOIN_PAYLOAD_SIZE 4u

typedef struct net_repl_join {
    uint32_t client_nonce;
} net_repl_join_t;

int net_repl_join_encode(const net_repl_join_t *msg, uint8_t *out_payload, size_t out_size);
int net_repl_join_decode(net_repl_join_t *msg, const uint8_t *payload, size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_JOIN_H */
