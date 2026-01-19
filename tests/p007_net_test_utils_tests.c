#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/test_buffer.h"
#include "ferrum/net/test_clock.h"
#include "ferrum/net/test_link.h"

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

static int test_clock_advances_deterministically(void) {
    net_test_clock_t clock;
    net_test_clock_init(&clock, 100u);
    ASSERT_UINT_EQ(100u, net_test_clock_now_ns(&clock));
    net_test_clock_advance(&clock, 25u);
    ASSERT_UINT_EQ(125u, net_test_clock_now_ns(&clock));
    return 0;
}

static int test_buffer_roundtrip(void) {
    uint8_t storage[16];
    net_test_buffer_t buffer;
    net_test_buffer_init(&buffer, storage, sizeof(storage));

    const char *msg = "abc";
    ASSERT_INT_EQ(NET_TEST_BUFFER_OK, net_test_buffer_write(&buffer, msg, 3u));

    char out[4] = {0};
    ASSERT_INT_EQ(NET_TEST_BUFFER_OK, net_test_buffer_read(&buffer, out, 3u));
    ASSERT_TRUE(memcmp(out, msg, 3u) == 0);
    return 0;
}

static int test_buffer_overflow_and_underflow(void) {
    uint8_t storage[4];
    net_test_buffer_t buffer;
    net_test_buffer_init(&buffer, storage, sizeof(storage));

    const uint8_t data[4] = {1u, 2u, 3u, 4u};
    ASSERT_INT_EQ(NET_TEST_BUFFER_OK, net_test_buffer_write(&buffer, data, 4u));
    ASSERT_INT_EQ(NET_TEST_BUFFER_ERR_OVERFLOW, net_test_buffer_write(&buffer, data, 1u));

    uint8_t out[5];
    ASSERT_INT_EQ(NET_TEST_BUFFER_ERR_UNDERFLOW, net_test_buffer_read(&buffer, out, 5u));
    return 0;
}

static int test_link_scripted_loss_dup_reorder_jitter(void) {
    net_test_clock_t clock;
    net_test_clock_init(&clock, 0u);

    net_test_step_t steps[] = {
        {1u, 0u, 0u},
        {0u, 0u, 0u},
        {2u, 5u, 3u},
        {1u, 2u, 0u},
    };

    net_test_link_t link;
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_init(&link, &clock, steps, ARRAY_SIZE(steps), 8u, 8u));

    const uint8_t payload_a[] = {'A'};
    const uint8_t payload_b[] = {'B'};
    const uint8_t payload_c[] = {'C'};
    const uint8_t payload_d[] = {'D'};

    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_send(&link, payload_a, sizeof(payload_a)));
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_send(&link, payload_b, sizeof(payload_b)));
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_send(&link, payload_c, sizeof(payload_c)));
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_send(&link, payload_d, sizeof(payload_d)));

    uint8_t out[8];
    size_t out_size = 0u;

    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_receive(&link, out, sizeof(out), &out_size));
    ASSERT_UINT_EQ(1u, out_size);
    ASSERT_TRUE(out[0] == 'A');

    ASSERT_INT_EQ(NET_TEST_LINK_EMPTY, net_test_link_receive(&link, out, sizeof(out), &out_size));

    net_test_clock_advance(&clock, 2u);
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_receive(&link, out, sizeof(out), &out_size));
    ASSERT_TRUE(out[0] == 'D');

    net_test_clock_advance(&clock, 3u);
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_receive(&link, out, sizeof(out), &out_size));
    ASSERT_TRUE(out[0] == 'C');

    net_test_clock_advance(&clock, 3u);
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_receive(&link, out, sizeof(out), &out_size));
    ASSERT_TRUE(out[0] == 'C');

    net_test_link_destroy(&link);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"clock_advances_deterministically", test_clock_advances_deterministically},
    {"buffer_roundtrip", test_buffer_roundtrip},
    {"buffer_overflow_and_underflow", test_buffer_overflow_and_underflow},
    {"link_scripted_loss_dup_reorder_jitter", test_link_scripted_loss_dup_reorder_jitter},
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
