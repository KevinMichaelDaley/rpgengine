#include <stdlib.h>
#include <string.h>

#include "ferrum/net/replication/join.h"
#include "ferrum/net/rudp/peer.h"

#include "runtime_internal.h"

static int packet_extract_join_nonce_(const uint8_t *packet, size_t packet_size, uint32_t *out_nonce) {
    if (!packet || !out_nonce) {
        return 0;
    }

    /* Do not decode protocol frames here; treat JOIN as an inbound message. */
    net_rudp_send_slot_t send_slots[1u];
    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer,
                                   NET_RUDP_PROTOCOL_ID_P008,
                                   50u,
                                   send_slots,
                                   (size_t)(sizeof(send_slots) / sizeof(send_slots[0])));

    uint8_t reliable = 0u;
    uint16_t schema_id = 0u;
    uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];
    size_t payload_size = 0u;
    if (net_rudp_peer_receive(&peer,
                              packet,
                              packet_size,
                              0u, /* now_ms — not needed for temp peer */
                              &reliable,
                              &schema_id,
                              payload,
                              sizeof(payload),
                              &payload_size) != NET_RUDP_OK) {
        return 0;
    }

    if (schema_id != NET_REPL_SCHEMA_JOIN) {
        return 0;
    }

    net_repl_join_t join;
    if (net_repl_join_decode(&join, payload, payload_size) != NET_REPL_OK) {
        return 0;
    }
    *out_nonce = join.client_nonce;
    return 1;
}

static uint64_t fnv1a64_(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0u; i < size; ++i) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t transport_key_from_addr_(const net_udp_addr_t *a) {
    if (!a || a->len == 0u) {
        return 0ull;
    }

    /* Hash only the meaningful bytes. Do NOT compare/hash full sockaddr_storage
       buffers (padding is unspecified). This key is transient and only used for
       demuxing packets to a per-client fiber.
     */
    const size_t n = (a->len <= sizeof(a->storage)) ? (size_t)a->len : sizeof(a->storage);
    return fnv1a64_(a->storage, n);
}

static int find_client_by_transport_key_(fr_server_net_runtime_t *rt, uint64_t key,
                                         const net_udp_addr_t *addr) {
    if (!rt || key == 0ull) {
        return -1;
    }
    for (uint16_t i = 0u; i < rt->cfg.max_clients; ++i) {
        if (rt->clients[i].active && rt->clients[i].transport_key == key) {
            /* Verify address matches to guard against FNV1a hash collisions. */
            if (addr && rt->clients[i].addr.len == addr->len &&
                memcmp(rt->clients[i].addr.storage, addr->storage, addr->len) == 0) {
                return (int)i;
            }
        }
    }
    return -1;
}

static int find_client_by_nonce_(fr_server_net_runtime_t *rt, uint32_t client_nonce) {
    if (!rt) {
        return -1;
    }
    for (uint16_t i = 0u; i < rt->cfg.max_clients; ++i) {
        if (rt->clients[i].active && rt->clients[i].auth_client_nonce == client_nonce) {
            return (int)i;
        }
    }
    return -1;
}

static int alloc_client_(fr_server_net_runtime_t *rt, const net_udp_addr_t *from, uint32_t client_nonce) {
    for (uint16_t i = 0u; i < rt->cfg.max_clients; ++i) {
        if (!rt->clients[i].active) {
            rt->clients[i].active = 1u;
            rt->clients[i].addr = *from;
            rt->clients[i].auth_client_nonce = client_nonce;
            rt->clients[i].transport_key = transport_key_from_addr_(from);
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

            /* Start the client fiber without hard pinning.
               Some sharded schedulers can starve other long-lived fibers when pinned.
             */
                if (job_dispatch_named(rt->cfg.jobs, fr_server_client_fiber_main, args, 0, NULL, "Net.Server.HandlingClientUDP") == JOB_ID_INVALID) {
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

        const uint64_t tkey = transport_key_from_addr_(&from);
        int idx = find_client_by_transport_key_(rt, tkey, &from);

        if (idx < 0) {
            /* New transport endpoint: must be a JOIN so we can assign mocked
               persistent identity (nonce). */
            uint32_t nonce = 0u;
            if (!packet_extract_join_nonce_(packet, size, &nonce)) {
                continue;
            }

            /* If this nonce already exists, update its transport key/address.
               Identity is the nonce; transport is expected to be stable in tests.
             */
            idx = find_client_by_nonce_(rt, nonce);
            if (idx >= 0) {
                rt->clients[idx].addr = from;
                rt->clients[idx].transport_key = tkey;
            } else {
                idx = alloc_client_(rt, &from, nonce);
                if (idx < 0) {
                    continue;
                }
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
