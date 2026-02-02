#ifndef FERRUM_NET_REPLICATION_WELCOME_H
#define FERRUM_NET_REPLICATION_WELCOME_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"

/** @file
 * @brief WELCOME message: server -> client (reliable by resend).
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NET_REPL_WELCOME_PAYLOAD_SIZE 4u

typedef struct net_repl_welcome {
    uint16_t expected_entities;
    uint16_t tick_hz;
} net_repl_welcome_t;

int net_repl_welcome_encode(const net_repl_welcome_t *msg, uint8_t *out_payload, size_t out_size);
int net_repl_welcome_decode(net_repl_welcome_t *msg, const uint8_t *payload, size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_WELCOME_H */
