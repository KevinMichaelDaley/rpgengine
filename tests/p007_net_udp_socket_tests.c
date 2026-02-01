#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/udp_socket.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(exp), (int)(act));                                                             \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                                                         \
    do {                                                                                                 \
        if ((uint64_t)(exp) != (uint64_t)(act)) {                                                         \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %llu got %llu\n", __FILE__,          \
                    __LINE__, (unsigned long long)(exp), (unsigned long long)(act));                     \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int test_udp_open_close_is_idempotent(void) {
    net_udp_socket_t sock;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_open(&sock));
    net_udp_socket_close(&sock);
    net_udp_socket_close(&sock);
    return 0;
}

static int test_udp_invalid_args(void) {
    ASSERT_INT_EQ(NET_UDP_SOCKET_ERR_INVALID, net_udp_socket_open(NULL));
    net_udp_socket_close(NULL);
    ASSERT_INT_EQ(NET_UDP_SOCKET_ERR_INVALID, net_udp_addr_ipv4(NULL, 127u, 0u, 0u, 1u, 1234u));
    return 0;
}

static int test_udp_loopback_send_recv_roundtrip(void) {
    net_udp_socket_t server;
    net_udp_socket_t client;

    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_open(&server));
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_open(&client));

    net_udp_addr_t bind_addr;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_addr_ipv4(&bind_addr, 127u, 0u, 0u, 1u, 0u));
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_bind(&server, &bind_addr));

    net_udp_addr_t server_addr;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_local_addr(&server, &server_addr));

    const uint8_t payload[] = {1u, 2u, 3u, 4u, 5u};
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_sendto(&client, &server_addr, payload, sizeof(payload)));

    uint8_t out[64];
    size_t out_size = 0u;
    net_udp_addr_t from;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK,
                  net_udp_socket_recvfrom(&server, &from, out, sizeof(out), &out_size));

    ASSERT_UINT_EQ(sizeof(payload), out_size);
    ASSERT_TRUE(memcmp(out, payload, sizeof(payload)) == 0);

    net_udp_socket_close(&client);
    net_udp_socket_close(&server);
    return 0;
}

static int test_udp_nonblocking_empty(void) {
    net_udp_socket_t sock;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_open(&sock));

    net_udp_addr_t bind_addr;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_addr_ipv4(&bind_addr, 127u, 0u, 0u, 1u, 0u));
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_bind(&sock, &bind_addr));

    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_set_nonblocking(&sock, 1));

    uint8_t out[32];
    size_t out_size = 0u;
    net_udp_addr_t from;
    ASSERT_INT_EQ(NET_UDP_SOCKET_EMPTY, net_udp_socket_recvfrom(&sock, &from, out, sizeof(out), &out_size));

    net_udp_socket_close(&sock);
    return 0;
}

static int test_udp_connected_send_recv(void) {
    net_udp_socket_t server;
    net_udp_socket_t client;

    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_open(&server));
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_open(&client));

    net_udp_addr_t server_bind;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_addr_ipv4(&server_bind, 127u, 0u, 0u, 1u, 0u));
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_bind(&server, &server_bind));

    net_udp_addr_t server_addr;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_local_addr(&server, &server_addr));

    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_connect(&client, &server_addr));

    const uint8_t ping[] = {9u, 8u, 7u};
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_send(&client, ping, sizeof(ping)));

    uint8_t out[64];
    size_t out_size = 0u;
    net_udp_addr_t from;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_recvfrom(&server, &from, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(sizeof(ping), out_size);
    ASSERT_TRUE(memcmp(out, ping, sizeof(ping)) == 0);

    /* Server can now treat this as an established peer (no handshake here; just OS-level connect). */
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_connect(&server, &from));

    const uint8_t pong[] = {1u, 2u, 3u, 4u};
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_send(&server, pong, sizeof(pong)));

    uint8_t out2[64];
    size_t out2_size = 0u;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_recv(&client, out2, sizeof(out2), &out2_size));
    ASSERT_UINT_EQ(sizeof(pong), out2_size);
    ASSERT_TRUE(memcmp(out2, pong, sizeof(pong)) == 0);

    net_udp_socket_close(&client);
    net_udp_socket_close(&server);
    return 0;
}

static int test_udp_recv_timeout(void) {
    net_udp_socket_t sock;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_open(&sock));

    net_udp_addr_t bind_addr;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_addr_ipv4(&bind_addr, 127u, 0u, 0u, 1u, 0u));
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_bind(&sock, &bind_addr));

    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_set_nonblocking(&sock, 0));
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_socket_set_recv_timeout_ms(&sock, 2u));

    uint8_t out[32];
    size_t out_size = 0u;
    net_udp_addr_t from;

    int rc = net_udp_socket_recvfrom(&sock, &from, out, sizeof(out), &out_size);
    ASSERT_TRUE(rc == NET_UDP_SOCKET_TIMEOUT || rc == NET_UDP_SOCKET_EMPTY);

    net_udp_socket_close(&sock);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"udp_open_close_is_idempotent", test_udp_open_close_is_idempotent},
    {"udp_invalid_args", test_udp_invalid_args},
    {"udp_loopback_send_recv_roundtrip", test_udp_loopback_send_recv_roundtrip},
    {"udp_nonblocking_empty", test_udp_nonblocking_empty},
    {"udp_connected_send_recv", test_udp_connected_send_recv},
    {"udp_recv_timeout", test_udp_recv_timeout},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0u;
    for (size_t i = 0u; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
