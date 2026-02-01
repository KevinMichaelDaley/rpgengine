#include <stdint.h>
#include <stdio.h>

#include "ferrum/net/reliable_channel.h"

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

static int test_reliable_in_order_delivery(void) {
    net_reliable_channel_t channel;
    net_reliable_channel_init(&channel, 8u, 8u);

    const uint8_t a[] = {1u};
    const uint8_t b[] = {2u};
    const uint8_t c[] = {3u};

    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_send(&channel, a, sizeof(a)));
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_send(&channel, b, sizeof(b)));
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_send(&channel, c, sizeof(c)));

    uint8_t out[8];
    size_t out_size = 0u;
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_receive(&channel, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(1u, out_size);
    ASSERT_UINT_EQ(1u, out[0]);
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_receive(&channel, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(2u, out[0]);
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_receive(&channel, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(3u, out[0]);

    net_reliable_channel_destroy(&channel);
    return 0;
}

static int test_reliable_duplicate_is_ignored(void) {
    net_reliable_channel_t channel;
    net_reliable_channel_init(&channel, 4u, 4u);

    const uint8_t data[] = {9u};
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_send(&channel, data, sizeof(data)));
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_resend(&channel, 0u));

    uint8_t out[4];
    size_t out_size = 0u;
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_receive(&channel, out, sizeof(out), &out_size));
    ASSERT_INT_EQ(NET_RELIABLE_EMPTY, net_reliable_channel_receive(&channel, out, sizeof(out), &out_size));

    net_reliable_channel_destroy(&channel);
    return 0;
}

static int test_reliable_out_of_order_buffering(void) {
    net_reliable_channel_t channel;
    net_reliable_channel_init(&channel, 8u, 8u);

    const uint8_t a[] = {1u};
    const uint8_t b[] = {2u};
    const uint8_t c[] = {3u};

    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_send_sequence(&channel, 1u, b, sizeof(b)));
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_send_sequence(&channel, 2u, c, sizeof(c)));
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_send_sequence(&channel, 0u, a, sizeof(a)));

    uint8_t out[8];
    size_t out_size = 0u;
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_receive(&channel, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(1u, out[0]);
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_receive(&channel, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(2u, out[0]);
    ASSERT_INT_EQ(NET_RELIABLE_OK, net_reliable_channel_receive(&channel, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(3u, out[0]);

    net_reliable_channel_destroy(&channel);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"reliable_in_order_delivery", test_reliable_in_order_delivery},
    {"reliable_duplicate_is_ignored", test_reliable_duplicate_is_ignored},
    {"reliable_out_of_order_buffering", test_reliable_out_of_order_buffering},
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
