#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/test_clock.h"
#include "ferrum/net/test_link.h"
#include "ferrum/net/test_transport.h"
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

static int test_link_next_delivery_time_empty_returns_false(void) {
    net_test_clock_t clock;
    net_test_clock_init(&clock, 0u);

    net_test_step_t steps[] = {{1u, 0u, 0u}};

    net_test_link_t link;
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_init(&link, &clock, steps, ARRAY_SIZE(steps), 8u, 16u));

    uint64_t t = 0u;
    ASSERT_TRUE(!net_test_link_next_delivery_time_ns(&link, &t));

    net_test_link_destroy(&link);
    return 0;
}

static int test_link_next_delivery_time_tracks_earliest_enqueued(void) {
    net_test_clock_t clock;
    net_test_clock_init(&clock, 0u);

    net_test_step_t steps[] = {
        {1u, 5u, 0u},
        {1u, 2u, 0u},
    };

    net_test_link_t link;
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_init(&link, &clock, steps, ARRAY_SIZE(steps), 8u, 8u));

    const uint8_t a[] = {'A'};
    const uint8_t b[] = {'B'};

    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_send(&link, a, sizeof(a)));
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_send(&link, b, sizeof(b)));

    uint64_t t = 0u;
    ASSERT_TRUE(net_test_link_next_delivery_time_ns(&link, &t));
    ASSERT_UINT_EQ(2u, t);

    net_test_link_destroy(&link);
    return 0;
}

static int test_transport_sendto_routes_to_correct_client(void) {
    fr_test_transport_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_clients = 2u;
    cfg.base_port = 9000u;
    cfg.clock_start_ns = 0u;
    cfg.link_slots = 8u;
    cfg.max_payload_size = 16u;

    net_test_step_t steps[] = {{1u, 0u, 0u}};
    cfg.client_to_server_steps = steps;
    cfg.client_to_server_step_count = ARRAY_SIZE(steps);
    cfg.server_to_client_steps = steps;
    cfg.server_to_client_step_count = ARRAY_SIZE(steps);

    fr_test_transport_t *t = fr_test_transport_create(&cfg);
    ASSERT_TRUE(t != NULL);

    const uint8_t x[] = {'X'};
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, fr_test_transport_sendto_cb(t, &t->client_addrs[1], x, sizeof(x)));

    uint8_t out[16];
    size_t out_size = 0u;
    ASSERT_INT_EQ(NET_TEST_LINK_EMPTY, net_test_link_receive(&t->server_to_client_links[0], out, sizeof(out), &out_size));

    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_receive(&t->server_to_client_links[1], out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(1u, out_size);
    ASSERT_TRUE(out[0] == 'X');

    fr_test_transport_destroy(t);
    return 0;
}

static int test_transport_recvfrom_picks_earliest_delivery_across_clients(void) {
    fr_test_transport_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_clients = 2u;
    cfg.base_port = 9100u;
    cfg.clock_start_ns = 0u;
    cfg.link_slots = 8u;
    cfg.max_payload_size = 16u;

    net_test_step_t default_steps[] = {{1u, 0u, 0u}};
    cfg.client_to_server_steps = default_steps;
    cfg.client_to_server_step_count = ARRAY_SIZE(default_steps);
    cfg.server_to_client_steps = default_steps;
    cfg.server_to_client_step_count = ARRAY_SIZE(default_steps);

    fr_test_transport_t *t = fr_test_transport_create(&cfg);
    ASSERT_TRUE(t != NULL);

    net_test_step_t slow[] = {{1u, 5u, 0u}};
    net_test_step_t fast[] = {{1u, 0u, 0u}};

    t->client_to_server_links[0].steps = slow;
    t->client_to_server_links[0].step_count = ARRAY_SIZE(slow);
    t->client_to_server_links[0].step_index = 0u;

    t->client_to_server_links[1].steps = fast;
    t->client_to_server_links[1].step_count = ARRAY_SIZE(fast);
    t->client_to_server_links[1].step_index = 0u;

    const uint8_t a[] = {'A'};
    const uint8_t b[] = {'B'};

    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_send(&t->client_to_server_links[0], a, sizeof(a)));
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_send(&t->client_to_server_links[1], b, sizeof(b)));

    net_test_clock_advance(&t->clock, 5u);

    uint8_t packet[16];
    size_t packet_size = 0u;
    net_udp_addr_t from;

    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, fr_test_transport_recvfrom_cb(t, &from, packet, sizeof(packet), &packet_size));
    ASSERT_UINT_EQ(1u, packet_size);
    ASSERT_TRUE(packet[0] == 'B');
    ASSERT_TRUE(addr_equal_(&from, &t->client_addrs[1]));

    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, fr_test_transport_recvfrom_cb(t, &from, packet, sizeof(packet), &packet_size));
    ASSERT_UINT_EQ(1u, packet_size);
    ASSERT_TRUE(packet[0] == 'A');
    ASSERT_TRUE(addr_equal_(&from, &t->client_addrs[0]));

    ASSERT_INT_EQ(NET_UDP_SOCKET_EMPTY, fr_test_transport_recvfrom_cb(t, &from, packet, sizeof(packet), &packet_size));

    fr_test_transport_destroy(t);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"link_next_delivery_time_empty_returns_false", test_link_next_delivery_time_empty_returns_false},
    {"link_next_delivery_time_tracks_earliest_enqueued", test_link_next_delivery_time_tracks_earliest_enqueued},
    {"transport_sendto_routes_to_correct_client", test_transport_sendto_routes_to_correct_client},
    {"transport_recvfrom_picks_earliest_delivery_across_clients", test_transport_recvfrom_picks_earliest_delivery_across_clients},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0u;
    for (size_t i = 0u; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("PASS %s\n", tc->name);
            passed++;
        } else {
            printf("FAIL %s\n", tc->name);
            return 1;
        }
    }
    printf("%zu/%zu tests passed\n", passed, total);
    return 0;
}
