#include <stdint.h>
#include <stdio.h>

#include "ferrum/net/ack_window.h"

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

static int test_ack_bits_mapping_and_duplicates(void) {
    net_ack_window_t window;
    net_ack_window_init(&window);

    ASSERT_INT_EQ(NET_ACK_WINDOW_OK, net_ack_window_receive(&window, 100u));
    ASSERT_UINT_EQ(100u, net_ack_window_ack(&window));
    ASSERT_UINT_EQ(0u, net_ack_window_ack_bits(&window));

    ASSERT_INT_EQ(NET_ACK_WINDOW_OK, net_ack_window_receive(&window, 99u));
    ASSERT_UINT_EQ(100u, net_ack_window_ack(&window));
    ASSERT_UINT_EQ(0x1u, net_ack_window_ack_bits(&window));

    ASSERT_INT_EQ(NET_ACK_WINDOW_OK, net_ack_window_receive(&window, 98u));
    ASSERT_UINT_EQ(0x3u, net_ack_window_ack_bits(&window));

    ASSERT_INT_EQ(NET_ACK_WINDOW_DUPLICATE, net_ack_window_receive(&window, 99u));
    ASSERT_UINT_EQ(0x3u, net_ack_window_ack_bits(&window));
    return 0;
}

static int test_out_of_window_is_ignored(void) {
    net_ack_window_t window;
    net_ack_window_init(&window);

    ASSERT_INT_EQ(NET_ACK_WINDOW_OK, net_ack_window_receive(&window, 100u));
    ASSERT_INT_EQ(NET_ACK_WINDOW_OK, net_ack_window_receive(&window, 99u));

    ASSERT_INT_EQ(NET_ACK_WINDOW_OUT_OF_WINDOW, net_ack_window_receive(&window, 60u));
    ASSERT_UINT_EQ(100u, net_ack_window_ack(&window));
    ASSERT_UINT_EQ(0x1u, net_ack_window_ack_bits(&window));
    return 0;
}

static int test_sequence_wraparound_updates(void) {
    net_ack_window_t window;
    net_ack_window_init(&window);

    ASSERT_INT_EQ(NET_ACK_WINDOW_OK, net_ack_window_receive(&window, 65534u));
    ASSERT_INT_EQ(NET_ACK_WINDOW_OK, net_ack_window_receive(&window, 65535u));
    ASSERT_UINT_EQ(65535u, net_ack_window_ack(&window));
    ASSERT_UINT_EQ(0x1u, net_ack_window_ack_bits(&window));

    ASSERT_INT_EQ(NET_ACK_WINDOW_OK, net_ack_window_receive(&window, 0u));
    ASSERT_UINT_EQ(0u, net_ack_window_ack(&window));
    ASSERT_UINT_EQ(0x3u, net_ack_window_ack_bits(&window));

    ASSERT_INT_EQ(NET_ACK_WINDOW_OK, net_ack_window_receive(&window, 1u));
    ASSERT_UINT_EQ(1u, net_ack_window_ack(&window));
    ASSERT_UINT_EQ(0x7u, net_ack_window_ack_bits(&window));
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"ack_bits_mapping_and_duplicates", test_ack_bits_mapping_and_duplicates},
    {"out_of_window_is_ignored", test_out_of_window_is_ignored},
    {"sequence_wraparound_updates", test_sequence_wraparound_updates},
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
