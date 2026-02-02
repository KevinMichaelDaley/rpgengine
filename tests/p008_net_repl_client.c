#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <time.h>

#include <unistd.h>

#include "ferrum/net/udp_socket.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/spawn.h"
#include "ferrum/net/replication/state_cube.h"

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static void sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000u * 1000u);
    nanosleep(&ts, NULL);
}

static int parse_ipv4_dotted(const char *s, uint8_t out[4]) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!s) {
        return 0;
    }
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0;
    }
    if (a > 255u || b > 255u || c > 255u || d > 255u) {
        return 0;
    }
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
    return 1;
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s <server_ipv4> <port> <duration_ms> <expected_spawns>\n", argv0);
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int has_entity(const uint32_t *ids, size_t count, uint32_t id) {
    for (size_t i = 0u; i < count; ++i) {
        if (ids[i] == id) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        usage(argv[0]);
        return 2;
    }

    uint8_t ip[4];
    if (!parse_ipv4_dotted(argv[1], ip)) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", argv[1]);
        return 2;
    }
    long port_l = strtol(argv[2], NULL, 10);
    long duration_ms_l = strtol(argv[3], NULL, 10);
    long expected_spawns_l = strtol(argv[4], NULL, 10);
    if (port_l <= 0 || port_l > 65535 || duration_ms_l <= 0 || expected_spawns_l <= 0) {
        fprintf(stderr, "Invalid arguments\n");
        return 2;
    }

    net_udp_addr_t server_addr;
    if (net_udp_addr_ipv4(&server_addr, ip[0], ip[1], ip[2], ip[3], (uint16_t)port_l) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to build server address\n");
        return 1;
    }

    net_udp_socket_t sock;
    if (net_udp_socket_open(&sock) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to open UDP socket\n");
        return 1;
    }
    (void)net_udp_socket_set_nonblocking(&sock, 1);
    if (net_udp_socket_connect(&sock, &server_addr) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to connect\n");
        net_udp_socket_close(&sock);
        return 1;
    }

    net_rudp_peer_t peer;
    net_rudp_peer_init(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u);

    uint32_t rng = (uint32_t)(0xC001D00Du ^ (uint32_t)getpid());
    net_repl_join_t join;
    join.client_nonce = xorshift32(&rng);
    uint8_t join_payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    if (net_repl_join_encode(&join, join_payload, sizeof(join_payload)) != NET_REPL_OK) {
        fprintf(stderr, "Failed to encode JOIN\n");
        net_udp_socket_close(&sock);
        return 1;
    }

    uint16_t join_seq = 0u;
    if (net_rudp_peer_send_reliable(&peer,
                                   &sock,
                                   &server_addr,
                                   now_ms(),
                                   NET_REPL_SCHEMA_JOIN,
                                   join_payload,
                                   sizeof(join_payload),
                                   &join_seq) != NET_RUDP_OK) {
        fprintf(stderr, "Failed to send JOIN\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    (void)join_seq;

    const uint64_t start = now_ms();
    const uint64_t end = start + (uint64_t)duration_ms_l;
    uint64_t next_keepalive = start;
    uint32_t spawn_count = 0u;
    uint32_t state_count = 0u;

    uint32_t entity_ids[256];
    size_t entity_count = 0u;
    memset(entity_ids, 0, sizeof(entity_ids));

    uint8_t rx_packet[NET_RUDP_MAX_PACKET_SIZE];
    while (now_ms() < end) {
        uint64_t now = now_ms();

        if (now >= next_keepalive) {
            (void)net_rudp_peer_send_unreliable(&peer,
                                                &sock,
                                                &server_addr,
                                                now,
                                                NET_REPL_SCHEMA_JOIN,
                                                join_payload,
                                                sizeof(join_payload));
            next_keepalive = now + 100u;
        }

        (void)net_rudp_peer_tick_resend(&peer, &sock, &server_addr, now);

        size_t rx_size = 0u;
        int rrc = net_udp_socket_recv(&sock, rx_packet, sizeof(rx_packet), &rx_size);
        if (rrc == NET_UDP_SOCKET_EMPTY) {
            sleep_ms(1u);
            continue;
        }
        if (rrc != NET_UDP_SOCKET_OK) {
            break;
        }

        uint8_t reliable = 0u;
        uint16_t schema_id = 0u;
        uint8_t payload[256];
        size_t payload_size = 0u;
        int prc = net_rudp_peer_receive(&peer,
                                        rx_packet,
                                        rx_size,
                                        &reliable,
                                        &schema_id,
                                        payload,
                                        sizeof(payload),
                                        &payload_size);
        if (prc != NET_RUDP_OK) {
            continue;
        }
        (void)reliable;

        if (schema_id == NET_REPL_SCHEMA_SPAWN) {
            net_repl_spawn_t sp;
            if (net_repl_spawn_decode(&sp, payload, payload_size) == NET_REPL_OK) {
                spawn_count++;
                if (!has_entity(entity_ids, entity_count, sp.entity_id) && entity_count < 256u) {
                    entity_ids[entity_count++] = sp.entity_id;
                }
            }
        } else if (schema_id == NET_REPL_SCHEMA_STATE_CUBE) {
            net_repl_state_cube_t st;
            if (net_repl_state_cube_decode(&st, payload, payload_size) == NET_REPL_OK) {
                state_count++;
            }
        }
    }

    const uint32_t expected_spawns = (uint32_t)expected_spawns_l;
    if (entity_count < (size_t)expected_spawns) {
        fprintf(stderr, "Client failed: expected %u spawns, got %zu (spawn_msgs=%u state_msgs=%u)\n",
                (unsigned)expected_spawns,
                entity_count,
                (unsigned)spawn_count,
                (unsigned)state_count);
        net_udp_socket_close(&sock);
        return 1;
    }
    if (state_count < expected_spawns * 5u) {
        fprintf(stderr, "Client failed: too few state updates (%u)\n", (unsigned)state_count);
        net_udp_socket_close(&sock);
        return 1;
    }

    net_udp_socket_close(&sock);
    return 0;
}
