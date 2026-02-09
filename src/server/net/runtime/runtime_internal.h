#ifndef FERRUM_SERVER_NET_RUNTIME_INTERNAL_H
#define FERRUM_SERVER_NET_RUNTIME_INTERNAL_H

#include <stdatomic.h>
#include <stdint.h>

#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/server/net/runtime.h"

/* Keep per-client inbox stack usage modest; tune as needed for throughput. */
#define FR_SERVER_CLIENT_INBOX_SLOTS 64u

typedef struct fr_server_client_inbox_t {
    atomic_uint read_idx;
    atomic_uint write_idx;
    uint16_t sizes[FR_SERVER_CLIENT_INBOX_SLOTS];
    uint8_t packets[FR_SERVER_CLIENT_INBOX_SLOTS][NET_RUDP_MAX_PACKET_SIZE];
} fr_server_client_inbox_t;

typedef struct fr_server_client_t {
    uint8_t active;
    net_udp_addr_t addr;

    /* Mock authentication identity until a real auth system exists.
       For now we treat JOIN.client_nonce as the persistent identity.
     */
    uint32_t auth_client_nonce;

        /* Transient transport key for demuxing packets to this client.
             Must not be used as persistent identity.
         */
        uint64_t transport_key;

    atomic_uintptr_t inbox_ptr; /* (fr_server_client_inbox_t*) stored by fiber */

    /* One-packet staging area until fiber publishes its stack inbox pointer. */
    atomic_bool pending_used;
    uint16_t pending_size;
    uint8_t pending_packet[NET_RUDP_MAX_PACKET_SIZE];

    fr_topic_channel_t *out_reliable;
    fr_topic_channel_t *out_unreliable;

    /** Pre-allocated RUDP reliable send slots for the client fiber. */
    net_rudp_send_slot_t *send_slots;
    size_t send_slot_count;

    atomic_bool stop;
    atomic_uint_least64_t now_ms;
} fr_server_client_t;

typedef struct fr_server_client_fiber_args_t {
    struct fr_server_net_runtime *rt;
    uint16_t client_id;
} fr_server_client_fiber_args_t;

struct fr_server_net_runtime {
    fr_server_net_runtime_config_t cfg;

    job_system_t owned_jobs;
    uint8_t owns_jobs;

    fr_server_client_t *clients;

    atomic_uint_least64_t packets_in;
    atomic_uint_least64_t packets_out;
    atomic_uint_least64_t bytes_in;
    atomic_uint_least64_t bytes_out;
};

bool fr_server_client_inbox_try_push(fr_server_client_inbox_t *inbox, const uint8_t *packet, size_t size);
bool fr_server_client_inbox_try_pop(fr_server_client_inbox_t *inbox, uint8_t *out_packet, size_t cap, size_t *out_size);

void fr_server_client_fiber_main(void *user);

#endif /* FERRUM_SERVER_NET_RUNTIME_INTERNAL_H */
