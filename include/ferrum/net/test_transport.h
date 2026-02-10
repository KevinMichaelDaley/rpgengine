/**
 * @file
 * @brief In-process UDP transport simulation for server+client integration tests.
 *
 * This module bridges deterministic net_test_link_t links into callbacks compatible
 * with fr_server_net_runtime's sendto_cb/recvfrom_cb so integration tests can run
 * without OS sockets.
 */
#ifndef FERRUM_NET_TEST_TRANSPORT_H
#define FERRUM_NET_TEST_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/test_clock.h"
#include "ferrum/net/test_link.h"
#include "ferrum/net/udp_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Configuration for a test transport. */
typedef struct fr_test_transport_config {
    /** Number of simulated clients (>= 1). */
    uint16_t max_clients;

    /** Base port for generating deterministic client addresses. */
    uint16_t base_port;

    /** Initial clock time in nanoseconds. */
    uint64_t clock_start_ns;

    /** Link storage: max queued packets per directed link. */
    size_t link_slots;

    /** Link storage: maximum payload size in bytes. */
    size_t max_payload_size;

    /** Script steps for client->server links (may be NULL if count == 0). */
    const net_test_step_t *client_to_server_steps;

    /** Number of client->server script steps. */
    size_t client_to_server_step_count;

    /** Script steps for server->client links (may be NULL if count == 0). */
    const net_test_step_t *server_to_client_steps;

    /** Number of server->client script steps. */
    size_t server_to_client_step_count;
} fr_test_transport_config_t;

/** In-process transport simulation. */
typedef struct fr_test_transport {
    /** Clock shared by all links. Tests advance this to deliver delayed packets. */
    net_test_clock_t clock;

    /** Number of clients. */
    uint16_t max_clients;

    /** Per-client address table (length=max_clients). */
    net_udp_addr_t *client_addrs;

    /** Client->server links (length=max_clients). */
    net_test_link_t *client_to_server_links;

    /** Server->client links (length=max_clients). */
    net_test_link_t *server_to_client_links;
} fr_test_transport_t;

/** Create a transport and initialize all links. Returns NULL on error. */
fr_test_transport_t *fr_test_transport_create(const fr_test_transport_config_t *cfg);

/** Destroy a transport (NULL-safe). */
void fr_test_transport_destroy(fr_test_transport_t *t);

/** recvfrom callback compatible with fr_server_net_runtime_config_t::recvfrom_cb. */
int fr_test_transport_recvfrom_cb(void *user,
                                 net_udp_addr_t *out_from,
                                 uint8_t *out_data,
                                 size_t out_cap,
                                 size_t *out_size);

/** sendto callback compatible with fr_server_net_runtime_config_t::sendto_cb. */
int fr_test_transport_sendto_cb(void *user, const net_udp_addr_t *to, const void *data, size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_TEST_TRANSPORT_H */
