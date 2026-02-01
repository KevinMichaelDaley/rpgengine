#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include "ferrum/net/udp_socket.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define MAGIC_FRM1 0x46524D31u
#define VERSION_1 1u

enum msg_type {
    MSG_HELLO = 1u,
    MSG_WELCOME = 2u,
    MSG_STATE = 3u,
    MSG_ECHO = 4u,
    MSG_STOP = 5u,
    MSG_STOP_ACK = 6u,
};

static void write_u32_be(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)((v >> 24) & 0xFFu);
    out[1] = (uint8_t)((v >> 16) & 0xFFu);
    out[2] = (uint8_t)((v >> 8) & 0xFFu);
    out[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t read_u32_be(const uint8_t *in) {
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | (uint32_t)in[3];
}

static int addr_equal(const net_udp_addr_t *a, const net_udp_addr_t *b) {
    if (!a || !b) {
        return 0;
    }
    if (a->len != b->len) {
        return 0;
    }
    return memcmp(a->storage, b->storage, a->len) == 0;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

struct client_slot {
    net_udp_addr_t addr;
    uint32_t nonce;
    uint8_t active;
    uint64_t last_seen_ms;
};

static int find_or_add_client(struct client_slot *slots, size_t count, const net_udp_addr_t *addr) {
    for (size_t i = 0u; i < count; ++i) {
        if (slots[i].active && addr_equal(&slots[i].addr, addr)) {
            return (int)i;
        }
    }
    for (size_t i = 0u; i < count; ++i) {
        if (!slots[i].active) {
            slots[i].addr = *addr;
            slots[i].nonce = 0u;
            slots[i].active = 1u;
            slots[i].last_seen_ms = now_ms();
            return (int)i;
        }
    }
    return -1;
}

static size_t encode_header(uint8_t *out, size_t cap, uint8_t type) {
    if (!out || cap < 8u) {
        return 0u;
    }
    write_u32_be(out + 0, MAGIC_FRM1);
    out[4] = type;
    out[5] = VERSION_1;
    out[6] = 0u;
    out[7] = 0u;
    return 8u;
}

static int decode_header(const uint8_t *bytes, size_t size, uint8_t *out_type) {
    if (!bytes || size < 8u || !out_type) {
        return 0;
    }
    if (read_u32_be(bytes + 0) != MAGIC_FRM1) {
        return 0;
    }
    if (bytes[5] != VERSION_1) {
        return 0;
    }
    *out_type = bytes[4];
    return 1;
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s <port>\n", argv0);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        usage(argv[0]);
        return 2;
    }

    long port_l = strtol(argv[1], NULL, 10);
    if (port_l <= 0 || port_l > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return 2;
    }
    uint16_t port = (uint16_t)port_l;

    net_udp_socket_t sock;
    if (net_udp_socket_open(&sock) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to open UDP socket\n");
        return 1;
    }

    net_udp_addr_t bind_addr;
    if (net_udp_addr_ipv4(&bind_addr, 0u, 0u, 0u, 0u, port) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to build bind addr\n");
        return 1;
    }
    if (net_udp_socket_bind(&sock, &bind_addr) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to bind UDP socket\n");
        return 1;
    }

    (void)net_udp_socket_set_nonblocking(&sock, 1);

    fprintf(stdout, "p007_net_integration_server listening on UDP port %u\n", (unsigned)port);
    fflush(stdout);

    struct client_slot clients[64];
    memset(clients, 0, sizeof(clients));

    uint8_t packet[512];
    uint8_t reply[512];

    for (;;) {
        net_udp_addr_t from;
        size_t packet_size = 0u;
        int rc = net_udp_socket_recvfrom(&sock, &from, packet, sizeof(packet), &packet_size);
        if (rc == NET_UDP_SOCKET_EMPTY) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1 * 1000 * 1000; /* 1ms */
            nanosleep(&ts, NULL);
            continue;
        }
        if (rc != NET_UDP_SOCKET_OK) {
            fprintf(stderr, "recvfrom failed (rc=%d)\n", rc);
            break;
        }

        uint8_t type = 0u;
        if (!decode_header(packet, packet_size, &type)) {
            continue;
        }

        int slot = find_or_add_client(clients, ARRAY_SIZE(clients), &from);
        if (slot < 0) {
            continue;
        }
        clients[slot].last_seen_ms = now_ms();

        if (type == MSG_HELLO) {
            if (packet_size < 12u) {
                continue;
            }
            uint32_t nonce = read_u32_be(packet + 8);
            clients[slot].nonce = nonce;

            size_t n = encode_header(reply, sizeof(reply), MSG_WELCOME);
            write_u32_be(reply + n, nonce);
            n += 4u;
            (void)net_udp_socket_sendto(&sock, &from, reply, n);
            continue;
        }

        if (type == MSG_STATE) {
            /* Echo back exactly (unreliable channel semantics). */
            if (packet_size > sizeof(reply)) {
                continue;
            }
            memcpy(reply, packet, packet_size);
            reply[4] = MSG_ECHO;
            (void)net_udp_socket_sendto(&sock, &from, reply, packet_size);
            continue;
        }

        if (type == MSG_STOP) {
            if (packet_size < 12u) {
                continue;
            }
            uint32_t nonce = read_u32_be(packet + 8);
            if (clients[slot].nonce != 0u && nonce != clients[slot].nonce) {
                continue;
            }

            size_t n = encode_header(reply, sizeof(reply), MSG_STOP_ACK);
            write_u32_be(reply + n, nonce);
            n += 4u;
            (void)net_udp_socket_sendto(&sock, &from, reply, n);
            net_udp_socket_close(&sock);
            return 0;
        }
    }

    net_udp_socket_close(&sock);
    return 1;
}
