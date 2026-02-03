#include <stdlib.h>
#include <string.h>

#include "runtime_internal.h"

static int addr_equal_(const net_udp_addr_t *a, const net_udp_addr_t *b) {
    if (!a || !b) {
        return 0;
    }
    if (a->len != b->len) {
        return 0;
    }
    return memcmp(a->storage, b->storage, a->len) == 0;
}

static int find_client_(fr_server_net_runtime_t *rt, const net_udp_addr_t *from) {
    for (uint16_t i = 0u; i < rt->cfg.max_clients; ++i) {
        if (rt->clients[i].active && addr_equal_(&rt->clients[i].addr, from)) {
            return (int)i;
        }
    }
    return -1;
}

static int alloc_client_(fr_server_net_runtime_t *rt, const net_udp_addr_t *from) {
    for (uint16_t i = 0u; i < rt->cfg.max_clients; ++i) {
        if (!rt->clients[i].active) {
            rt->clients[i].active = 1u;
            rt->clients[i].addr = *from;
            atomic_store_explicit(&rt->clients[i].stop, false, memory_order_release);
            atomic_store_explicit(&rt->clients[i].inbox_ptr, (uintptr_t)0, memory_order_release);
            atomic_store_explicit(&rt->clients[i].pending_used, false, memory_order_release);
            rt->clients[i].pending_size = 0u;

            fr_server_client_fiber_args_t *args = (fr_server_client_fiber_args_t *)calloc(1u, sizeof(*args));
            if (!args) {
                rt->clients[i].active = 0u;
                return -1;
            }
            args->rt = rt;
            args->client_id = i;

            /* Start the client fiber on one of the first two workers (alternating).
               This satisfies the ">=2 dedicated workers" requirement when worker_count>=2.
             */
            uint32_t preferred = (uint32_t)(i % 2u);
            if (job_dispatch_to(rt->cfg.jobs, fr_server_client_fiber_main, args, 0, NULL, preferred) == JOB_ID_INVALID) {
                free(args);
                rt->clients[i].active = 0u;
                return -1;
            }
            return (int)i;
        }
    }
    return -1;
}

static int recvfrom_(fr_server_net_runtime_t *rt,
                     net_udp_addr_t *out_from,
                     uint8_t *out_packet,
                     size_t cap,
                     size_t *out_size) {
    if (rt->cfg.recvfrom_cb) {
        return rt->cfg.recvfrom_cb(rt->cfg.io_user, out_from, out_packet, cap, out_size);
    }
    return net_udp_socket_recvfrom(rt->cfg.socket, out_from, out_packet, cap, out_size);
}

bool fr_server_net_runtime_pump(fr_server_net_runtime_t *rt, uint64_t now_ms) {
    if (!rt) {
        return false;
    }

    uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
    for (;;) {
        net_udp_addr_t from;
        size_t size = 0u;
        int rc = recvfrom_(rt, &from, packet, sizeof(packet), &size);
        if (rc == NET_UDP_SOCKET_EMPTY || rc == NET_UDP_SOCKET_TIMEOUT) {
            break;
        }
        if (rc != NET_UDP_SOCKET_OK) {
            break;
        }

        atomic_fetch_add_explicit(&rt->packets_in, 1u, memory_order_relaxed);
        atomic_fetch_add_explicit(&rt->bytes_in, (uint64_t)size, memory_order_relaxed);

        int idx = find_client_(rt, &from);
        if (idx < 0) {
            idx = alloc_client_(rt, &from);
            if (idx < 0) {
                continue;
            }
        }

        atomic_store_explicit(&rt->clients[idx].now_ms, now_ms, memory_order_release);

        fr_server_client_inbox_t *inbox = (fr_server_client_inbox_t *)(uintptr_t)atomic_load_explicit(&rt->clients[idx].inbox_ptr, memory_order_acquire);
        if (!inbox) {
            /* Fiber not yet initialized its stack inbox; stage one packet. */
            bool expected = false;
            if (atomic_compare_exchange_strong_explicit(&rt->clients[idx].pending_used,
                                                       &expected,
                                                       true,
                                                       memory_order_acq_rel,
                                                       memory_order_acquire)) {
                if (size <= NET_RUDP_MAX_PACKET_SIZE) {
                    memcpy(rt->clients[idx].pending_packet, packet, size);
                    rt->clients[idx].pending_size = (uint16_t)size;
                } else {
                    atomic_store_explicit(&rt->clients[idx].pending_used, false, memory_order_release);
                }
            }
            continue;
        }
        (void)fr_server_client_inbox_try_push(inbox, packet, size);
    }

    /* Update now_ms for active clients even when no packets arrived. */
    for (uint16_t i = 0u; i < rt->cfg.max_clients; ++i) {
        if (rt->clients[i].active) {
            atomic_store_explicit(&rt->clients[i].now_ms, now_ms, memory_order_release);
        }
    }

    return true;
}
