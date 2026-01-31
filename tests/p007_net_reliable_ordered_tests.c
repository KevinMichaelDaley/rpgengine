#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/reliable_ordered_channel.h"
#include "ferrum/net/test_clock.h"
#include "ferrum/net/test_link.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((int)(exp) != (int)(act)) {                                                                  \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,    \
                    (int)(exp), (int)(act));                                                             \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                                                         \
    do {                                                                                                 \
        if ((uint64_t)(exp) != (uint64_t)(act)) {                                                         \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %llu got %llu\n", __FILE__,         \
                    __LINE__, (unsigned long long)(exp), (unsigned long long)(act));                     \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int pump_links(net_reliable_ordered_channel_t *a,
                      net_reliable_ordered_channel_t *b,
                      net_test_clock_t *clock,
                      net_test_link_t *a_to_b,
                      net_test_link_t *b_to_a,
                      uint64_t max_steps) {
    uint8_t packet[2048];
    size_t packet_size = 0u;

    for (uint64_t step = 0u; step < max_steps; ++step) {
        /* A -> link */
        while (net_reliable_ordered_channel_next_packet(a, net_test_clock_now_ns(clock), packet, sizeof(packet),
                                                        &packet_size) == NET_RELIABLE_ORDERED_OK) {
            ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_send(a_to_b, packet, packet_size));
        }

        /* B -> link */
        while (net_reliable_ordered_channel_next_packet(b, net_test_clock_now_ns(clock), packet, sizeof(packet),
                                                        &packet_size) == NET_RELIABLE_ORDERED_OK) {
            ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_send(b_to_a, packet, packet_size));
        }

        /* link -> B */
        while (net_test_link_receive(a_to_b, packet, sizeof(packet), &packet_size) == NET_TEST_LINK_OK) {
            ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK,
                          net_reliable_ordered_channel_handle_packet(b, packet, packet_size, net_test_clock_now_ns(clock)));
        }

        /* link -> A */
        while (net_test_link_receive(b_to_a, packet, sizeof(packet), &packet_size) == NET_TEST_LINK_OK) {
            ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK,
                          net_reliable_ordered_channel_handle_packet(a, packet, packet_size, net_test_clock_now_ns(clock)));
        }

        net_test_clock_advance(clock, 1000u);
    }

    return 0;
}

static int test_reliable_ordered_in_order_delivery(void) {
    net_reliable_ordered_channel_t a;
    net_reliable_ordered_channel_t b;

    ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_init(&a, 0x11223344u, 1200u, 4096u, 50u * 1000u));
    ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_init(&b, 0x11223344u, 1200u, 4096u, 50u * 1000u));

    net_test_clock_t clock;
    net_test_clock_init(&clock, 0u);

    net_test_step_t steps[] = {
        {1u, 0u, 0u},
    };

    net_test_link_t a_to_b;
    net_test_link_t b_to_a;
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_init(&a_to_b, &clock, steps, ARRAY_SIZE(steps), 64u, 2048u));
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_init(&b_to_a, &clock, steps, ARRAY_SIZE(steps), 64u, 2048u));

    for (uint32_t i = 1u; i <= 32u; ++i) {
        uint8_t msg[4];
        msg[0] = (uint8_t)(i & 0xFFu);
        msg[1] = (uint8_t)((i >> 8) & 0xFFu);
        msg[2] = 0xAAu;
        msg[3] = 0x55u;
        ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_send(&a, msg, sizeof(msg)));
    }

    ASSERT_INT_EQ(0, pump_links(&a, &b, &clock, &a_to_b, &b_to_a, 5000u));

    for (uint32_t i = 1u; i <= 32u; ++i) {
        uint8_t out[16];
        size_t out_size = 0u;
        ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_receive(&b, out, sizeof(out), &out_size));
        ASSERT_UINT_EQ(4u, out_size);
        uint32_t got = (uint32_t)out[0] | ((uint32_t)out[1] << 8);
        ASSERT_UINT_EQ(i, got);
        ASSERT_TRUE(out[2] == 0xAAu && out[3] == 0x55u);
    }

    {
        uint8_t out[16];
        size_t out_size = 0u;
        ASSERT_INT_EQ(NET_RELIABLE_ORDERED_EMPTY, net_reliable_ordered_channel_receive(&b, out, sizeof(out), &out_size));
    }

    net_test_link_destroy(&a_to_b);
    net_test_link_destroy(&b_to_a);
    net_reliable_ordered_channel_destroy(&a);
    net_reliable_ordered_channel_destroy(&b);
    return 0;
}

static int test_reliable_ordered_loss_dup_reorder_with_resend(void) {
    net_reliable_ordered_channel_t a;
    net_reliable_ordered_channel_t b;

    ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_init(&a, 0x11223344u, 1200u, 4096u, 10u * 1000u));
    ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_init(&b, 0x11223344u, 1200u, 4096u, 10u * 1000u));

    net_test_clock_t clock;
    net_test_clock_init(&clock, 0u);

    /* Script: drop, delay, duplicate to force retransmit + dedupe + reorder. */
    net_test_step_t steps_a_to_b[] = {
        {0u, 0u, 0u},
        {1u, 4000u, 0u},
        {2u, 0u, 2000u},
        {1u, 1000u, 0u},
    };
    net_test_step_t steps_b_to_a[] = {
        {1u, 0u, 0u},
        {1u, 1000u, 0u},
    };

    net_test_link_t a_to_b;
    net_test_link_t b_to_a;
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_init(&a_to_b, &clock, steps_a_to_b, ARRAY_SIZE(steps_a_to_b), 128u, 2048u));
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_init(&b_to_a, &clock, steps_b_to_a, ARRAY_SIZE(steps_b_to_a), 128u, 2048u));

    for (uint32_t i = 1u; i <= 16u; ++i) {
        uint8_t msg[2] = {(uint8_t)i, (uint8_t)(0xF0u ^ (uint8_t)i)};
        ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_send(&a, msg, sizeof(msg)));
    }

    ASSERT_INT_EQ(0, pump_links(&a, &b, &clock, &a_to_b, &b_to_a, 20000u));

    for (uint32_t i = 1u; i <= 16u; ++i) {
        uint8_t out[8];
        size_t out_size = 0u;
        ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_receive(&b, out, sizeof(out), &out_size));
        ASSERT_UINT_EQ(2u, out_size);
        ASSERT_UINT_EQ((uint64_t)i, (uint64_t)out[0]);
        ASSERT_UINT_EQ((uint64_t)(0xF0u ^ (uint8_t)i), (uint64_t)out[1]);
    }

    {
        uint8_t out[8];
        size_t out_size = 0u;
        ASSERT_INT_EQ(NET_RELIABLE_ORDERED_EMPTY, net_reliable_ordered_channel_receive(&b, out, sizeof(out), &out_size));
    }

    net_test_link_destroy(&a_to_b);
    net_test_link_destroy(&b_to_a);
    net_reliable_ordered_channel_destroy(&a);
    net_reliable_ordered_channel_destroy(&b);
    return 0;
}

static int test_reliable_ordered_reassembly_before_delivery(void) {
    net_reliable_ordered_channel_t a;
    net_reliable_ordered_channel_t b;

    /* Small max packet forces fragmentation. */
    ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_init(&a, 0x11223344u, 64u, 4096u, 10u * 1000u));
    ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_init(&b, 0x11223344u, 64u, 4096u, 10u * 1000u));

    net_test_clock_t clock;
    net_test_clock_init(&clock, 0u);

    /* Force reordering: delayed copy then immediate copy. */
    net_test_step_t steps[] = {
        {1u, 5000u, 0u},
        {1u, 0u, 0u},
        {1u, 2000u, 0u},
        {1u, 0u, 0u},
    };

    net_test_link_t a_to_b;
    net_test_link_t b_to_a;
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_init(&a_to_b, &clock, steps, ARRAY_SIZE(steps), 256u, 2048u));
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_init(&b_to_a, &clock, steps, ARRAY_SIZE(steps), 256u, 2048u));

    uint8_t big[512];
    for (size_t i = 0u; i < sizeof(big); ++i) {
        big[i] = (uint8_t)(i & 0xFFu);
    }

    ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_send(&a, big, sizeof(big)));
    ASSERT_INT_EQ(0, pump_links(&a, &b, &clock, &a_to_b, &b_to_a, 50000u));

    uint8_t out[600];
    size_t out_size = 0u;
    ASSERT_INT_EQ(NET_RELIABLE_ORDERED_OK, net_reliable_ordered_channel_receive(&b, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(sizeof(big), out_size);
    ASSERT_TRUE(memcmp(big, out, sizeof(big)) == 0);

    net_test_link_destroy(&a_to_b);
    net_test_link_destroy(&b_to_a);
    net_reliable_ordered_channel_destroy(&a);
    net_reliable_ordered_channel_destroy(&b);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"reliable_ordered_in_order_delivery", test_reliable_ordered_in_order_delivery},
    {"reliable_ordered_loss_dup_reorder_with_resend", test_reliable_ordered_loss_dup_reorder_with_resend},
    {"reliable_ordered_reassembly_before_delivery", test_reliable_ordered_reassembly_before_delivery},
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
