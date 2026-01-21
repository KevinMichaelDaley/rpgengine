#include <stdint.h>
#include <stdio.h>

#include "ferrum/net/unreliable_channel.h"

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

static int test_unreliable_enqueue_dequeue_order(void) {
    net_unreliable_channel_t channel;
    net_unreliable_channel_init(&channel, 4u, 8u);

    const uint8_t a[] = {1u};
    const uint8_t b[] = {2u, 3u};
    const uint8_t c[] = {4u, 5u, 6u};

    ASSERT_INT_EQ(NET_UNRELIABLE_OK, net_unreliable_channel_send(&channel, a, sizeof(a)));
    ASSERT_INT_EQ(NET_UNRELIABLE_OK, net_unreliable_channel_send(&channel, b, sizeof(b)));
    ASSERT_INT_EQ(NET_UNRELIABLE_OK, net_unreliable_channel_send(&channel, c, sizeof(c)));

    uint8_t out[8];
    size_t out_size = 0u;
    ASSERT_INT_EQ(NET_UNRELIABLE_OK, net_unreliable_channel_receive(&channel, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(sizeof(a), out_size);
    ASSERT_TRUE(out[0] == 1u);

    ASSERT_INT_EQ(NET_UNRELIABLE_OK, net_unreliable_channel_receive(&channel, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(sizeof(b), out_size);
    ASSERT_TRUE(out[0] == 2u && out[1] == 3u);

    ASSERT_INT_EQ(NET_UNRELIABLE_OK, net_unreliable_channel_receive(&channel, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(sizeof(c), out_size);
    ASSERT_TRUE(out[0] == 4u && out[1] == 5u && out[2] == 6u);

    net_unreliable_channel_destroy(&channel);
    return 0;
}

static int test_unreliable_buffer_exhaustion(void) {
    net_unreliable_channel_t channel;
    net_unreliable_channel_init(&channel, 2u, 4u);

    const uint8_t data[] = {9u, 8u, 7u, 6u};
    ASSERT_INT_EQ(NET_UNRELIABLE_OK, net_unreliable_channel_send(&channel, data, sizeof(data)));
    ASSERT_INT_EQ(NET_UNRELIABLE_OK, net_unreliable_channel_send(&channel, data, sizeof(data)));
    ASSERT_INT_EQ(NET_UNRELIABLE_ERR_FULL, net_unreliable_channel_send(&channel, data, sizeof(data)));

    net_unreliable_channel_destroy(&channel);
    return 0;
}

static int test_unreliable_rejects_oversized_payload(void) {
    net_unreliable_channel_t channel;
    net_unreliable_channel_init(&channel, 2u, 2u);

    const uint8_t data[] = {1u, 2u, 3u};
    ASSERT_INT_EQ(NET_UNRELIABLE_ERR_INVALID, net_unreliable_channel_send(&channel, data, sizeof(data)));

    net_unreliable_channel_destroy(&channel);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"unreliable_enqueue_dequeue_order", test_unreliable_enqueue_dequeue_order},
    {"unreliable_buffer_exhaustion", test_unreliable_buffer_exhaustion},
    {"unreliable_rejects_oversized_payload", test_unreliable_rejects_oversized_payload},
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
