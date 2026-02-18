/**
 * @file runtime_query.c
 * @brief Read-only queries on the server net runtime.
 *
 * Non-static functions: 2 (client_addr, socket).
 */

#include "runtime_internal.h"
#include <string.h>

bool fr_server_net_runtime_client_addr(const fr_server_net_runtime_t *rt,
                                       uint16_t client_id,
                                       net_udp_addr_t *out_addr) {
    if (!rt || !out_addr || client_id >= rt->cfg.max_clients) {
        return false;
    }
    if (!rt->clients[client_id].active) {
        return false;
    }
    memcpy(out_addr, &rt->clients[client_id].addr, sizeof(*out_addr));
    return true;
}

net_udp_socket_t *fr_server_net_runtime_socket(fr_server_net_runtime_t *rt) {
    if (!rt) { return NULL; }
    return rt->cfg.socket;
}
