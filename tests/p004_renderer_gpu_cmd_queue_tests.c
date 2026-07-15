/**
 * @file p004_renderer_gpu_cmd_queue_tests.c
 * @brief Unit + concurrency tests for the GPU command queue (bounded MPSC ring).
 *
 * The queue is the boundary between loader FIBERS (many producers, CPU-side
 * asset work) and the render/main thread (single consumer that executes GL). No
 * dynamic allocation: the ring storage is caller-provided.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/resource/gpu_cmd.h"
#include "ferrum/renderer/resource/gpu_cmd_queue.h"

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "ASSERT failed %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

/* Backing storage helper: one queue over `cap` slots. */
#define QUEUE(name, states, cap)                                              \
    gpu_cmd_t name##_slots[cap];                                              \
    atomic_int states[cap];                                                   \
    gpu_cmd_queue_t name;                                                     \
    gpu_cmd_queue_init(&name, name##_slots, states, (cap))

/* Push/pop a single command round-trips all fields in FIFO order. */
static int test_push_pop_fifo(void) {
    QUEUE(q, st, 4);
    ASSERT_TRUE(gpu_cmd_queue_count(&q) == 0);

    for (uint32_t i = 0; i < 3; ++i) {
        gpu_cmd_t c = { 0 };
        c.type = GPU_CMD_UPLOAD_TEXTURE;
        c.a = i; c.b = 100 + i;
        ASSERT_TRUE(gpu_cmd_push(&q, &c));
    }
    ASSERT_TRUE(gpu_cmd_queue_count(&q) == 3);

    for (uint32_t i = 0; i < 3; ++i) {
        gpu_cmd_t out;
        ASSERT_TRUE(gpu_cmd_pop(&q, &out));
        ASSERT_TRUE(out.type == GPU_CMD_UPLOAD_TEXTURE);
        ASSERT_TRUE(out.a == i && out.b == 100 + i); /* FIFO order preserved. */
    }
    ASSERT_TRUE(gpu_cmd_queue_count(&q) == 0);
    return 0;
}

/* Pop on an empty queue fails; the out param is untouched. */
static int test_pop_empty(void) {
    QUEUE(q, st, 4);
    gpu_cmd_t out; out.type = GPU_CMD_NONE;
    ASSERT_TRUE(!gpu_cmd_pop(&q, &out));
    ASSERT_TRUE(out.type == GPU_CMD_NONE);
    return 0;
}

/* Filling to capacity then pushing once more fails; the queue is unchanged. */
static int test_full_rejects(void) {
    QUEUE(q, st, 4);
    gpu_cmd_t c = { 0 };
    for (uint32_t i = 0; i < 4; ++i) { c.a = i; ASSERT_TRUE(gpu_cmd_push(&q, &c)); }
    ASSERT_TRUE(gpu_cmd_queue_count(&q) == 4);
    ASSERT_TRUE(!gpu_cmd_push(&q, &c)); /* full */
    ASSERT_TRUE(gpu_cmd_queue_count(&q) == 4);
    return 0;
}

/* Steady push-1/pop-1 for many rounds wraps the ring far past capacity while
 * keeping FIFO order (exercises index wrap + slot-state reuse). */
static int test_wraparound(void) {
    QUEUE(q, st, 4);
    for (uint32_t n = 0; n < 100; ++n) {
        gpu_cmd_t c = { 0 }; c.a = n;
        ASSERT_TRUE(gpu_cmd_push(&q, &c));
        gpu_cmd_t out;
        ASSERT_TRUE(gpu_cmd_pop(&q, &out));
        ASSERT_TRUE(out.a == n);
    }
    ASSERT_TRUE(gpu_cmd_queue_count(&q) == 0);
    return 0;
}

/* NULL args are safe and reported as failure. */
static int test_null_safe(void) {
    QUEUE(q, st, 4);
    gpu_cmd_t c = { 0 };
    ASSERT_TRUE(!gpu_cmd_push(NULL, &c));
    ASSERT_TRUE(!gpu_cmd_push(&q, NULL));
    ASSERT_TRUE(!gpu_cmd_pop(NULL, &c));
    ASSERT_TRUE(!gpu_cmd_pop(&q, NULL));
    ASSERT_TRUE(gpu_cmd_queue_count(NULL) == 0);
    return 0;
}

/* ── Concurrency: N producer threads, this-thread single consumer. Every pushed
 * command must be popped exactly once (no loss, no duplication). ── */
#define NPROD 4
#define PER_PROD 4096
#define CAP 256

struct prod_arg { gpu_cmd_queue_t *q; uint32_t prod_id; };

static void *producer(void *ud) {
    struct prod_arg *a = (struct prod_arg *)ud;
    for (uint32_t i = 0; i < PER_PROD; ++i) {
        gpu_cmd_t c = { 0 };
        c.type = GPU_CMD_UPLOAD_TEXTURE;
        c.a = a->prod_id;
        c.b = i;
        while (!gpu_cmd_push(a->q, &c))
            sched_yield(); /* spin while the ring is full. */
    }
    return NULL;
}

static int test_mpsc_stress(void) {
    gpu_cmd_t slots[CAP];
    atomic_int states[CAP];
    gpu_cmd_queue_t q;
    gpu_cmd_queue_init(&q, slots, states, CAP);

    pthread_t th[NPROD];
    struct prod_arg args[NPROD];
    for (int p = 0; p < NPROD; ++p) {
        args[p].q = &q; args[p].prod_id = (uint32_t)p;
        pthread_create(&th[p], NULL, producer, &args[p]);
    }

    /* Consume until every producer's PER_PROD commands are seen. */
    static uint32_t seen[NPROD]; /* count per producer. */
    memset(seen, 0, sizeof seen);
    uint32_t total = NPROD * PER_PROD, got = 0;
    while (got < total) {
        gpu_cmd_t out;
        if (gpu_cmd_pop(&q, &out)) {
            ASSERT_TRUE(out.type == GPU_CMD_UPLOAD_TEXTURE);
            ASSERT_TRUE(out.a < NPROD);
            /* Per-producer commands arrive in the order they were pushed. */
            ASSERT_TRUE(out.b == seen[out.a]);
            seen[out.a]++;
            ++got;
        } else {
            sched_yield();
        }
    }
    for (int p = 0; p < NPROD; ++p) pthread_join(th[p], NULL);
    for (int p = 0; p < NPROD; ++p) ASSERT_TRUE(seen[p] == PER_PROD);
    ASSERT_TRUE(gpu_cmd_queue_count(&q) == 0);
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "push_pop_fifo", test_push_pop_fifo },
    { "pop_empty", test_pop_empty },
    { "full_rejects", test_full_rejects },
    { "wraparound", test_wraparound },
    { "null_safe", test_null_safe },
    { "mpsc_stress", test_mpsc_stress },
};

int main(void) {
    int failed = 0;
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int r = TESTS[i].fn();
        printf(r == 0 ? "OK   %s\n" : "FAIL %s\n", TESTS[i].name);
        failed += (r != 0);
    }
    printf("%s (%d failed)\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}
