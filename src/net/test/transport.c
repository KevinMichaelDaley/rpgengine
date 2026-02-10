#include <stdlib.h>
#include <string.h>

#include "ferrum/net/test_transport.h"

static int addr_equal_(const net_udp_addr_t *a, const net_udp_addr_t *b) {
    if (!a || !b) {
        return 0;
    }
    if (a->len != b->len) {
        return 0;
    }
    const size_t n = (a->len <= sizeof(a->storage)) ? (size_t)a->len : sizeof(a->storage);
    return memcmp(a->storage, b->storage, n) == 0;
}

fr_test_transport_t *fr_test_transport_create(const fr_test_transport_config_t *cfg) {
    if (!cfg || cfg->max_clients == 0u || cfg->base_port == 0u || cfg->link_slots == 0u || cfg->max_payload_size == 0u) {
        return NULL;
    }

    fr_test_transport_t *t = (fr_test_transport_t *)calloc(1u, sizeof(*t));
    if (!t) {
        return NULL;
    }

    t->max_clients = cfg->max_clients;
    net_test_clock_init(&t->clock, cfg->clock_start_ns);

    t->client_addrs = (net_udp_addr_t *)calloc(t->max_clients, sizeof(net_udp_addr_t));
    t->client_to_server_links = (net_test_link_t *)calloc(t->max_clients, sizeof(net_test_link_t));
    t->server_to_client_links = (net_test_link_t *)calloc(t->max_clients, sizeof(net_test_link_t));

    if (!t->client_addrs || !t->client_to_server_links || !t->server_to_client_links) {
        fr_test_transport_destroy(t);
        return NULL;
    }

    for (uint16_t i = 0u; i < t->max_clients; ++i) {
        if (net_udp_addr_ipv4(&t->client_addrs[i], 127u, 0u, 0u, 1u, (uint16_t)(cfg->base_port + i)) != NET_UDP_SOCKET_OK) {
            fr_test_transport_destroy(t);
            return NULL;
        }

        if (net_test_link_init(&t->client_to_server_links[i],
                              &t->clock,
                              cfg->client_to_server_steps,
                              cfg->client_to_server_step_count,
                              cfg->link_slots,
                              cfg->max_payload_size) != NET_TEST_LINK_OK) {
            fr_test_transport_destroy(t);
            return NULL;
        }

        if (net_test_link_init(&t->server_to_client_links[i],
                              &t->clock,
                              cfg->server_to_client_steps,
                              cfg->server_to_client_step_count,
                              cfg->link_slots,
                              cfg->max_payload_size) != NET_TEST_LINK_OK) {
            fr_test_transport_destroy(t);
            return NULL;
        }
    }

    return t;
}

void fr_test_transport_destroy(fr_test_transport_t *t) {
    if (!t) {
        return;
    }

    if (t->client_to_server_links) {
        for (uint16_t i = 0u; i < t->max_clients; ++i) {
            net_test_link_destroy(&t->client_to_server_links[i]);
        }
    }

    if (t->server_to_client_links) {
        for (uint16_t i = 0u; i < t->max_clients; ++i) {
            net_test_link_destroy(&t->server_to_client_links[i]);
        }
    }

    free(t->client_addrs);
    free(t->client_to_server_links);
    free(t->server_to_client_links);
    free(t);
}

int fr_test_transport_recvfrom_cb(void *user,
                                 net_udp_addr_t *out_from,
                                 uint8_t *out_data,
                                 size_t out_cap,
                                 size_t *out_size) {
    fr_test_transport_t *t = (fr_test_transport_t *)user;
    if (!t || !out_from || !out_data || !out_size) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    const uint64_t now_ns = net_test_clock_now_ns(&t->clock);

    uint16_t best_client = 0u;
    uint64_t best_time = 0u;
    int have_best = 0;

    for (uint16_t i = 0u; i < t->max_clients; ++i) {
        uint64_t next_time = 0u;
        if (!net_test_link_next_delivery_time_ns(&t->client_to_server_links[i], &next_time)) {
            continue;
        }
        if (next_time > now_ns) {
            continue;
        }
        if (!have_best || next_time < best_time) {
            have_best = 1;
            best_time = next_time;
            best_client = i;
        }
    }

    if (!have_best) {
        return NET_UDP_SOCKET_EMPTY;
    }

    size_t size = 0u;
    int rc = net_test_link_receive(&t->client_to_server_links[best_client], out_data, out_cap, &size);
    if (rc == NET_TEST_LINK_EMPTY) {
        return NET_UDP_SOCKET_EMPTY;
    }
    if (rc != NET_TEST_LINK_OK) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    *out_from = t->client_addrs[best_client];
    *out_size = size;
    return NET_UDP_SOCKET_OK;
}

int fr_test_transport_sendto_cb(void *user, const net_udp_addr_t *to, const void *data, size_t size) {
    fr_test_transport_t *t = (fr_test_transport_t *)user;
    if (!t || !to || !data) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    for (uint16_t i = 0u; i < t->max_clients; ++i) {
        if (!addr_equal_(to, &t->client_addrs[i])) {
            continue;
        }

        int rc = net_test_link_send(&t->server_to_client_links[i], data, size);
        if (rc == NET_TEST_LINK_OK) {
            return NET_UDP_SOCKET_OK;
        }
        if (rc == NET_TEST_LINK_ERR_INVALID) {
            return NET_UDP_SOCKET_ERR_INVALID;
        }
        return NET_UDP_SOCKET_ERR_SYS;
    }

    return NET_UDP_SOCKET_ERR_ADDR;
}
