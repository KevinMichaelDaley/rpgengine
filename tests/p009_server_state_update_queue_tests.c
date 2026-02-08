#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>

#include "ferrum/server/net/state_update_queue.h"

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

static int test_empty_pop_returns_false(void) {
    fr_state_update_queue_config_t cfg = {0};
    cfg.capacity = 8u;
    cfg.max_payload_size = 16u;

    fr_state_update_queue_t *q = fr_state_update_queue_create(&cfg);
    ASSERT_TRUE(q != NULL);

    uint16_t client_id = 0;
    uint16_t schema_id = 0;
    uint8_t payload[16];
    uint16_t cap = (uint16_t)sizeof(payload);
    ASSERT_TRUE(!fr_state_update_queue_pop(q, &client_id, &schema_id, payload, &cap));

    fr_state_update_queue_destroy(q);
    return 0;
}

static int test_single_producer_fifo_order(void) {
    fr_state_update_queue_config_t cfg = {0};
    cfg.capacity = 16u;
    cfg.max_payload_size = 8u;

    fr_state_update_queue_t *q = fr_state_update_queue_create(&cfg);
    ASSERT_TRUE(q != NULL);

    const uint16_t client_id = 7u;
    const uint16_t schema_id = 42u;

    for (uint16_t seq = 0u; seq < 10u; ++seq) {
        uint8_t msg[2] = {(uint8_t)(seq & 0xFFu), (uint8_t)((seq >> 8u) & 0xFFu)};
        ASSERT_TRUE(fr_state_update_queue_push(q, client_id, schema_id, msg, (uint16_t)sizeof(msg)));
    }

    for (uint16_t seq = 0u; seq < 10u; ++seq) {
        uint16_t out_client = 0;
        uint16_t out_schema = 0;
        uint8_t out[8] = {0};
        uint16_t out_cap = (uint16_t)sizeof(out);

        ASSERT_TRUE(fr_state_update_queue_pop(q, &out_client, &out_schema, out, &out_cap));
        ASSERT_UINT_EQ(client_id, out_client);
        ASSERT_UINT_EQ(schema_id, out_schema);
        ASSERT_UINT_EQ(2u, out_cap);

        uint16_t got = (uint16_t)out[0] | ((uint16_t)out[1] << 8u);
        ASSERT_UINT_EQ(seq, got);
    }

    {
        uint16_t out_client = 0;
        uint16_t out_schema = 0;
        uint8_t out[8] = {0};
        uint16_t out_cap = (uint16_t)sizeof(out);
        ASSERT_TRUE(!fr_state_update_queue_pop(q, &out_client, &out_schema, out, &out_cap));
    }

    fr_state_update_queue_destroy(q);
    return 0;
}

struct prod_cons_ctx {
    fr_state_update_queue_t *q;
    uint32_t producers;
    uint32_t per_producer;
    atomic_uint produced;
    atomic_uint consumed;
    atomic_int failure;
    atomic_uint *last_seq;
};

struct producer_args {
    struct prod_cons_ctx *ctx;
    uint32_t producer_index;
};

static void *producer_main(void *arg) {
    struct producer_args *pa = (struct producer_args *)arg;
    struct prod_cons_ctx *ctx = pa->ctx;

    const uint16_t client_id = (uint16_t)(100u + pa->producer_index);
    const uint16_t schema_id = 1u;

    for (uint32_t seq = 0u; seq < ctx->per_producer; ++seq) {
        uint8_t payload[4];
        payload[0] = (uint8_t)(seq & 0xFFu);
        payload[1] = (uint8_t)((seq >> 8u) & 0xFFu);
        payload[2] = (uint8_t)((seq >> 16u) & 0xFFu);
        payload[3] = (uint8_t)((seq >> 24u) & 0xFFu);

        while (!fr_state_update_queue_push(ctx->q, client_id, schema_id, payload, (uint16_t)sizeof(payload))) {
            if (atomic_load_explicit(&ctx->failure, memory_order_relaxed)) {
                return (void *)(intptr_t)-1;
            }
            sched_yield();
        }

        atomic_fetch_add_explicit(&ctx->produced, 1u, memory_order_relaxed);
    }

    return NULL;
}

static void *consumer_main(void *arg) {
    struct prod_cons_ctx *ctx = (struct prod_cons_ctx *)arg;

    const uint32_t total = ctx->producers * ctx->per_producer;

    while (atomic_load_explicit(&ctx->consumed, memory_order_relaxed) < total) {
        uint16_t client_id = 0;
        uint16_t schema_id = 0;
        uint8_t payload[8];
        uint16_t cap = (uint16_t)sizeof(payload);

        if (!fr_state_update_queue_pop(ctx->q, &client_id, &schema_id, payload, &cap)) {
            sched_yield();
            continue;
        }

        if (schema_id != 1u || cap != 4u || client_id < 100u) {
            atomic_store_explicit(&ctx->failure, 1, memory_order_relaxed);
            return (void *)(intptr_t)-1;
        }

        const uint32_t seq = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8u) | ((uint32_t)payload[2] << 16u) |
                             ((uint32_t)payload[3] << 24u);
        const uint32_t idx = (uint32_t)(client_id - 100u);
        if (idx >= ctx->producers) {
            atomic_store_explicit(&ctx->failure, 1, memory_order_relaxed);
            return (void *)(intptr_t)-1;
        }

        const uint32_t prev = atomic_exchange_explicit(&ctx->last_seq[idx], seq, memory_order_relaxed);
        if (seq != 0u && seq <= prev) {
            atomic_store_explicit(&ctx->failure, 1, memory_order_relaxed);
            return (void *)(intptr_t)-1;
        }

        atomic_fetch_add_explicit(&ctx->consumed, 1u, memory_order_relaxed);
    }

    return NULL;
}

static int test_multi_producer_preserves_per_client_order(void) {
    fr_state_update_queue_config_t cfg = {0};
    cfg.capacity = 64u;
    cfg.max_payload_size = 8u;

    fr_state_update_queue_t *q = fr_state_update_queue_create(&cfg);
    ASSERT_TRUE(q != NULL);

    const uint32_t producers = 4u;
    const uint32_t per_producer = 2000u;

    atomic_uint last_seq[producers];
    for (uint32_t i = 0u; i < producers; ++i) {
        atomic_init(&last_seq[i], 0u);
    }

    struct prod_cons_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.q = q;
    ctx.producers = producers;
    ctx.per_producer = per_producer;
    atomic_init(&ctx.produced, 0u);
    atomic_init(&ctx.consumed, 0u);
    atomic_init(&ctx.failure, 0);
    ctx.last_seq = last_seq;

    pthread_t prod_threads[producers];
    struct producer_args args[producers];
    for (uint32_t i = 0u; i < producers; ++i) {
        args[i].ctx = &ctx;
        args[i].producer_index = i;
        ASSERT_TRUE(pthread_create(&prod_threads[i], NULL, producer_main, &args[i]) == 0);
    }

    pthread_t consumer_thread;
    ASSERT_TRUE(pthread_create(&consumer_thread, NULL, consumer_main, &ctx) == 0);

    for (uint32_t i = 0u; i < producers; ++i) {
        pthread_join(prod_threads[i], NULL);
    }

    pthread_join(consumer_thread, NULL);

    ASSERT_INT_EQ(0, atomic_load_explicit(&ctx.failure, memory_order_relaxed));
    ASSERT_UINT_EQ((uint64_t)(producers * per_producer), atomic_load_explicit(&ctx.produced, memory_order_relaxed));
    ASSERT_UINT_EQ((uint64_t)(producers * per_producer), atomic_load_explicit(&ctx.consumed, memory_order_relaxed));

    fr_state_update_queue_destroy(q);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"empty_pop_returns_false", test_empty_pop_returns_false},
    {"single_producer_fifo_order", test_single_producer_fifo_order},
    {"multi_producer_preserves_per_client_order", test_multi_producer_preserves_per_client_order},
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
