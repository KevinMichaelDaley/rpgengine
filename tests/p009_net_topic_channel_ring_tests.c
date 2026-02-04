#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/topic_channel.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
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

static int test_full_empty_and_boundary_integrity(void) {
    fr_topic_channel_config_t cfg = {0};
    cfg.capacity = 4; /* message count hint */
    cfg.capacity_bytes = 64; /* explicit small byte ring */
    cfg.max_message_size = 32;
    cfg.backpressure = FR_TOPIC_BACKPRESSURE_FAIL;

    fr_topic_channel_t *ch = fr_topic_channel_create(&cfg);
    ASSERT_TRUE(ch != NULL);

    const uint8_t a[] = {'A'};
    const uint8_t b[] = {'B', 'B'};
    const uint8_t c[] = {'C', 'C', 'C'};

    ASSERT_TRUE(fr_topic_channel_push(ch, a, sizeof(a)));
    ASSERT_TRUE(fr_topic_channel_push(ch, b, sizeof(b)));
    ASSERT_TRUE(fr_topic_channel_push(ch, c, sizeof(c)));

    uint8_t out[32] = {0};
    size_t cap = sizeof(out);
    ASSERT_TRUE(fr_topic_channel_pop(ch, out, &cap));
    ASSERT_UINT_EQ(1u, cap);
    ASSERT_TRUE(out[0] == 'A');

    cap = sizeof(out);
    ASSERT_TRUE(fr_topic_channel_pop(ch, out, &cap));
    ASSERT_UINT_EQ(2u, cap);
    ASSERT_TRUE(out[0] == 'B' && out[1] == 'B');

    cap = sizeof(out);
    ASSERT_TRUE(fr_topic_channel_pop(ch, out, &cap));
    ASSERT_UINT_EQ(3u, cap);
    ASSERT_TRUE(out[0] == 'C' && out[1] == 'C' && out[2] == 'C');

    cap = sizeof(out);
    ASSERT_TRUE(!fr_topic_channel_pop(ch, out, &cap));

    fr_topic_channel_destroy(ch);
    return 0;
}

static int test_wraparound_preserves_fifo(void) {
    fr_topic_channel_config_t cfg = {0};
    cfg.capacity = 8;
    cfg.capacity_bytes = 48; /* force wrap quickly */
    cfg.max_message_size = 16;
    cfg.backpressure = FR_TOPIC_BACKPRESSURE_FAIL;

    fr_topic_channel_t *ch = fr_topic_channel_create(&cfg);
    ASSERT_TRUE(ch != NULL);

    /* push/pop to move head forward; then push again to wrap tail */
    for (int i = 0; i < 6; ++i) {
        uint8_t msg[5] = {(uint8_t)('a' + i), (uint8_t)('0' + i), 'X', 'Y', 'Z'};
        ASSERT_TRUE(fr_topic_channel_push(ch, msg, sizeof(msg)));
        uint8_t out[16] = {0};
        size_t cap = sizeof(out);
        ASSERT_TRUE(fr_topic_channel_pop(ch, out, &cap));
        ASSERT_UINT_EQ(sizeof(msg), cap);
        ASSERT_TRUE(memcmp(out, msg, sizeof(msg)) == 0);
    }

    /* now fill with a few messages and drain */
    const char *msgs[] = {"ONE", "TWO", "THREE"};
    for (size_t i = 0; i < ARRAY_SIZE(msgs); ++i) {
        ASSERT_TRUE(fr_topic_channel_push(ch, (const uint8_t *)msgs[i], strlen(msgs[i])));
    }

    for (size_t i = 0; i < ARRAY_SIZE(msgs); ++i) {
        uint8_t out[16] = {0};
        size_t cap = sizeof(out);
        ASSERT_TRUE(fr_topic_channel_pop(ch, out, &cap));
        ASSERT_UINT_EQ(strlen(msgs[i]), cap);
        ASSERT_TRUE(memcmp(out, msgs[i], cap) == 0);
    }

    fr_topic_channel_destroy(ch);
    return 0;
}

static int test_backpressure_drop_oldest(void) {
    fr_topic_channel_config_t cfg = {0};
    cfg.capacity = 0;
    cfg.capacity_bytes = 15; /* exactly fits 3x (4-byte header + 1-byte payload) */
    cfg.max_message_size = 16;
    cfg.backpressure = FR_TOPIC_BACKPRESSURE_DROP_OLDEST;

    fr_topic_channel_t *ch = fr_topic_channel_create(&cfg);
    ASSERT_TRUE(ch != NULL);

    ASSERT_TRUE(fr_topic_channel_push(ch, (const uint8_t *)"A", 1));
    ASSERT_TRUE(fr_topic_channel_push(ch, (const uint8_t *)"B", 1));
    ASSERT_TRUE(fr_topic_channel_push(ch, (const uint8_t *)"C", 1));

    /* This should force dropping oldest until it fits. */
    ASSERT_TRUE(fr_topic_channel_push(ch, (const uint8_t *)"D", 1));

    ASSERT_UINT_EQ(1u, fr_topic_channel_stat_dropped(ch));

    uint8_t out[8];
    size_t cap = sizeof(out);

    ASSERT_TRUE(fr_topic_channel_pop(ch, out, &cap));
    ASSERT_UINT_EQ(1u, cap);
    ASSERT_TRUE(out[0] == 'B');

    cap = sizeof(out);
    ASSERT_TRUE(fr_topic_channel_pop(ch, out, &cap));
    ASSERT_TRUE(out[0] == 'C');

    cap = sizeof(out);
    ASSERT_TRUE(fr_topic_channel_pop(ch, out, &cap));
    ASSERT_TRUE(out[0] == 'D');

    fr_topic_channel_destroy(ch);
    return 0;
}

static int test_backpressure_drop_newest(void) {
    fr_topic_channel_config_t cfg = {0};
    cfg.capacity_bytes = 15; /* exactly fits 3x (4-byte header + 1-byte payload) */
    cfg.max_message_size = 16;
    cfg.backpressure = FR_TOPIC_BACKPRESSURE_DROP_NEWEST;

    fr_topic_channel_t *ch = fr_topic_channel_create(&cfg);
    ASSERT_TRUE(ch != NULL);

    ASSERT_TRUE(fr_topic_channel_push(ch, (const uint8_t *)"A", 1));
    ASSERT_TRUE(fr_topic_channel_push(ch, (const uint8_t *)"B", 1));
    ASSERT_TRUE(fr_topic_channel_push(ch, (const uint8_t *)"C", 1));

    /* Should fail (dropped newest), but increment drop metric. */
    ASSERT_TRUE(!fr_topic_channel_push(ch, (const uint8_t *)"D", 1));
    ASSERT_UINT_EQ(1u, fr_topic_channel_stat_dropped(ch));

    uint8_t out[8];
    size_t cap = sizeof(out);

    ASSERT_TRUE(fr_topic_channel_pop(ch, out, &cap));
    ASSERT_TRUE(out[0] == 'A');
    cap = sizeof(out);
    ASSERT_TRUE(fr_topic_channel_pop(ch, out, &cap));
    ASSERT_TRUE(out[0] == 'B');
    cap = sizeof(out);
    ASSERT_TRUE(fr_topic_channel_pop(ch, out, &cap));
    ASSERT_TRUE(out[0] == 'C');

    fr_topic_channel_destroy(ch);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"full_empty_and_boundary_integrity", test_full_empty_and_boundary_integrity},
    {"wraparound_preserves_fifo", test_wraparound_preserves_fifo},
    {"backpressure_drop_oldest", test_backpressure_drop_oldest},
    {"backpressure_drop_newest", test_backpressure_drop_newest},
};

int main(void) {
    size_t passed = 0u;
    for (size_t i = 0u; i < ARRAY_SIZE(TESTS); ++i) {
        printf("RUN %s\n", TESTS[i].name);
        fflush(stdout);
        int rc = TESTS[i].fn();
        if (rc == 0) {
            passed++;
            printf("OK %s\n", TESTS[i].name);
        } else {
            fprintf(stderr, "FAILED %s (rc=%d)\n", TESTS[i].name, rc);
            return 1;
        }
    }

    printf("All %zu tests passed\n", passed);
    return 0;
}
