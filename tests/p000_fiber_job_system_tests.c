#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <threads.h>
#include <stdatomic.h>

#include "ferrum/ferrum.h"

#define TEST_FAIL(msg, ...)                                                                         \
    do {                                                                                            \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__);               \
        return 1;                                                                                  \
    } while (0)

#define ASSERT_TRUE(cond)                                                                           \
    do {                                                                                            \
        if (!(cond)) {                                                                             \
            TEST_FAIL("%s", #cond);                                                               \
        }                                                                                           \
    } while (0)

#define ASSERT_EQ_INT(expected, actual)                                                             \
    do {                                                                                            \
        long long _exp = (long long)(expected);                                                     \
        long long _act = (long long)(actual);                                                       \
        if (_exp != _act) {                                                                        \
            TEST_FAIL("expected %lld got %lld", _exp, _act);                                      \
        }                                                                                           \
    } while (0)

#define ASSERT_EQ_UINT(expected, actual)                                                            \
    do {                                                                                            \
        unsigned long long _exp = (unsigned long long)(expected);                                   \
        unsigned long long _act = (unsigned long long)(actual);                                     \
        if (_exp != _act) {                                                                        \
            TEST_FAIL("expected %llu got %llu", _exp, _act);                                      \
        }                                                                                           \
    } while (0)

#define ASSERT_EQ_STR(expected, actual)                                                             \
    do {                                                                                            \
        if (strcmp((expected), (actual)) != 0) {                                                    \
            TEST_FAIL("expected '%s' got '%s'", (expected), (actual));                            \
        }                                                                                           \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static atomic_int g_failure_flag = 0;

#define JOB_FAIL(msg, ...)                                                                          \
    do {                                                                                            \
        fprintf(stderr, "JOB_FAIL %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__);          \
        atomic_store(&g_failure_flag, 1);                                                           \
        return;                                                                                    \
    } while (0)

#define JOB_ASSERT_TRUE(cond)                                                                       \
    do {                                                                                            \
        if (!(cond)) {                                                                             \
            JOB_FAIL("%s", #cond);                                                                \
        }                                                                                           \
    } while (0)

#define JOB_ASSERT_EQ_INT(expected, actual)                                                         \
    do {                                                                                            \
        long long _exp = (long long)(expected);                                                     \
        long long _act = (long long)(actual);                                                       \
        if (_exp != _act) {                                                                        \
            JOB_FAIL("expected %lld got %lld", _exp, _act);                                       \
        }                                                                                           \
    } while (0)

struct test_case {
    const char *name;
    int (*fn)(void);
};

static void busy_wait_ms(uint32_t ms) {
    const uint64_t target_ns = (uint64_t)ms * 1000000ULL;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t elapsed = (uint64_t)(now.tv_sec - start.tv_sec) * 1000000000ULL +
                           (uint64_t)(now.tv_nsec - start.tv_nsec);
        if (elapsed >= target_ns) {
            break;
        }
    }
}

/* ---------- Helpers for individual tests ---------- */
struct yield_ctx {
    int *trace;
    size_t *cursor;
};

static void yield_job_fn(void *user) {
    struct yield_ctx *p = (struct yield_ctx *)user;
    p->trace[(*p->cursor)++] = 1;
    job_yield();
    p->trace[(*p->cursor)++] = 2;
}

static void other_job_fn(void *user) {
    struct yield_ctx *p = (struct yield_ctx *)user;
    p->trace[(*p->cursor)++] = 3;
}

struct fan_ctx {
    job_system_t *sys;
    job_counter_t *counter;
    atomic_int *child_runs;
};

static void fan_child_fn(void *user) {
    atomic_int *slot = (atomic_int *)user;
    atomic_fetch_add_explicit(slot, 1, memory_order_relaxed);
}

static void fan_parent_fn(void *user) {
    struct fan_ctx *ctx = (struct fan_ctx *)user;
    for (size_t i = 0; i < 100; ++i) {
        JOB_ASSERT_TRUE(job_dispatch(ctx->sys, fan_child_fn, &ctx->child_runs[i], 0, ctx->counter) != JOB_ID_INVALID);
    }
    JOB_ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(ctx->counter, 0));
}

struct wait_parks_ctx {
    job_counter_t *counter;
    int *a_resumed;
    int *b_ran;
};

static void job_a_fn(void *user) {
    struct wait_parks_ctx *ctx = (struct wait_parks_ctx *)user;
    job_wait_status_t st = job_wait_counter(ctx->counter, 0);
    JOB_ASSERT_EQ_INT(JOB_WAIT_OK, st);
    *ctx->a_resumed = 1;
}

static void job_b_fn(void *user) {
    struct wait_parks_ctx *ctx = (struct wait_parks_ctx *)user;
    *ctx->b_ran = 1;
    JOB_ASSERT_EQ_INT(0, job_counter_dec(ctx->counter));
}

struct count_ctx {
    uint32_t *hits;
    size_t hits_count;
};

static void count_job_fn(void *user) {
    struct count_ctx *ctx = (struct count_ctx *)user;
    uint32_t wid = job_current_worker_id();
    if (wid < ctx->hits_count) {
        ctx->hits[wid]++;
    }
}

struct priority_ctx {
    int *order;
    size_t *cursor;
    int value;
};

static void record_priority_fn(void *user) {
    struct priority_ctx *ctx = (struct priority_ctx *)user;
    ctx->order[(*ctx->cursor)++] = ctx->value;
}

struct record_ctx {
    char *buf;
    size_t *cursor;
};

static void record_trace_fn(void *user) {
    struct record_ctx *ctx = (struct record_ctx *)user;
    ctx->buf[(*ctx->cursor)++] = 'A';
    job_yield();
    ctx->buf[(*ctx->cursor)++] = 'B';
}

static void sibling_trace_fn(void *user) {
    struct record_ctx *ctx = (struct record_ctx *)user;
    ctx->buf[(*ctx->cursor)++] = 'C';
}

struct wait_job_ctx {
    job_counter_t *counter;
    int *waited;
};

static void wait_immediate_fn(void *user) {
    struct wait_job_ctx *ctx = (struct wait_job_ctx *)user;
    job_wait_status_t st = job_wait_counter(ctx->counter, 0);
    JOB_ASSERT_EQ_INT(JOB_WAIT_OK, st);
    *ctx->waited = 1;
}

struct enqueue_ctx {
    int *order;
    size_t *cursor;
};

static void enqueue_job_fn(void *user) {
    struct enqueue_ctx *ctx = (struct enqueue_ctx *)user;
    int value = ctx->order[*ctx->cursor];
    ctx->order[(*ctx->cursor)++] = value;
}

struct slow_ctx {
    atomic_int *completed;
};

static void slow_job_fn(void *user) {
    struct slow_ctx *ctx = (struct slow_ctx *)user;
    busy_wait_ms(5);
    atomic_fetch_add_explicit(ctx->completed, 1, memory_order_relaxed);
}

static void noop_fn(void *user) {
    (void)user;
}

struct once_ctx {
    atomic_int *runs;
};

static void job_once_fn(void *user) {
    struct once_ctx *ctx = (struct once_ctx *)user;
    atomic_fetch_add_explicit(ctx->runs, 1, memory_order_relaxed);
}

struct wait_signal_ctx {
    job_counter_t *counter;
    volatile int *resumed;
};

static void wait_job_fn(void *user) {
    struct wait_signal_ctx *ctx = (struct wait_signal_ctx *)user;
    JOB_ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(ctx->counter, 0));
    *ctx->resumed = 1;
}

static void signal_job_fn(void *user) {
    struct wait_signal_ctx *ctx = (struct wait_signal_ctx *)user;
    busy_wait_ms(1);
    JOB_ASSERT_EQ_INT(0, job_counter_dec(ctx->counter));
}

struct task_slot_ctx {
    int *slot;
};

static void task_slot_fn(void *user) {
    struct task_slot_ctx *ctx = (struct task_slot_ctx *)user;
    (*ctx->slot)++;
}

struct stress_item {
    uint32_t idx;
    atomic_uint *completed;
    atomic_uchar *seen;
};

static void stress_item_fn(void *user) {
    struct stress_item *item = (struct stress_item *)user;

    /* Deterministic per-item "randomness" to mix yield patterns. */
    uint32_t x = item->idx * 747796405u + 2891336453u;
    x ^= x >> 16;
    uint32_t yields = (x & 3u);
    for (uint32_t i = 0; i < yields; ++i) {
        job_yield();
    }

    unsigned char prev = atomic_exchange_explicit(&item->seen[item->idx], 1, memory_order_relaxed);
    if (prev != 0) {
        atomic_store(&g_failure_flag, 1);
        return;
    }

    atomic_fetch_add_explicit(item->completed, 1u, memory_order_relaxed);
}

static void stress_noise_fn(void *user) {
    (void)user;
    /* Keep workers busy and force more interleavings. */
    for (int i = 0; i < 16; ++i) {
        job_yield();
    }
}

static int test_randomized_dispatch_yield_interleavings(void) {
    atomic_store(&g_failure_flag, 0);

    job_system_t sys_;
    job_system_t *sys = &sys_;
    job_system_create_status_t sys_create_status =  job_system_create(sys,4, 4096, 64 * 1024, 2048, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_TRUE(sys_create_status == JOB_CREATE_OK);
    ASSERT_EQ_INT(0, job_system_start(sys));

    const uint32_t n = 1800;
    struct stress_item *items = (struct stress_item *)calloc((size_t)n, sizeof(*items));
    atomic_uchar *seen = (atomic_uchar *)calloc((size_t)n, sizeof(*seen));
    ASSERT_TRUE(items != NULL);
    ASSERT_TRUE(seen != NULL);

    atomic_uint completed;
    atomic_init(&completed, 0u);

    for (uint32_t i = 0; i < n; ++i) {
        atomic_init(&seen[i], 0);
        items[i].idx = i;
        items[i].completed = &completed;
        items[i].seen = seen;
    }

    /* Add some long-lived yielders to create scheduling pressure. */
    for (int i = 0; i < 64; ++i) {
        ASSERT_TRUE(job_dispatch(sys, stress_noise_fn, NULL, 0, NULL) != JOB_ID_INVALID);
    }

    /* Inject work from the main thread to exercise the injection ring. */
    for (uint32_t i = 0; i < n; ++i) {
        ASSERT_TRUE(job_dispatch(sys, stress_item_fn, &items[i], 0, NULL) != JOB_ID_INVALID);
        if ((i & 15u) == 0u) {
            thrd_yield();
        }
    }

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
    ASSERT_EQ_UINT(n, atomic_load_explicit(&completed, memory_order_relaxed));

    for (uint32_t i = 0; i < n; ++i) {
        ASSERT_EQ_INT(1, (int)atomic_load_explicit(&seen[i], memory_order_relaxed));
    }

    free(seen);
    free(items);
    job_system_shutdown(sys);
    return 0;
}

/* ---------- Tests ---------- */
static int test_fiber_yield_resume_ordering(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 32, 4 * 1024, 1024, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_TRUE(sys_create_status == JOB_CREATE_OK);
    ASSERT_EQ_INT(0, job_system_start(sys));

    int trace[3] = {0, 0, 0};
    size_t cursor = 0;
    struct yield_ctx ctx = {trace, &cursor};

    job_counter_t counter;
    job_counter_init(&counter, 0);

    ASSERT_TRUE(job_dispatch(sys, yield_job_fn, &ctx, 0, &counter) != JOB_ID_INVALID);
    ASSERT_TRUE(job_dispatch(sys, other_job_fn, &ctx, 0, &counter) != JOB_ID_INVALID);

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_UINT(0, job_counter_value(&counter));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));

    ASSERT_EQ_INT(3, (int)cursor);
    ASSERT_EQ_INT(1, trace[0]);
    ASSERT_EQ_INT(3, trace[1]);
    ASSERT_EQ_INT(2, trace[2]);

    job_system_shutdown(sys);
    return 0;
}

static int test_fan_out_fan_in_counter(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,2, 2048, 64 * 1024, 2048, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_counter_t counter;
    job_counter_init(&counter, 0);

    atomic_int child_runs[100];
    for (size_t i = 0; i < 100; ++i) {
        atomic_init(&child_runs[i], 0);
    }
    struct fan_ctx ctx = {sys, &counter, child_runs};

    ASSERT_TRUE(job_dispatch(sys, fan_parent_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));

    for (size_t i = 0; i < 100; ++i) {
        ASSERT_EQ_INT(1, atomic_load_explicit(&child_runs[i], memory_order_relaxed));
    }

    job_system_shutdown(sys);
    return 0;
}

static int test_wait_parks_fiber_not_worker(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 16, 64 * 1024, 2048, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_counter_t counter;
    job_counter_init(&counter, 1);

    int b_ran = 0;
    int a_resumed = 0;
    struct wait_parks_ctx ctx = {&counter, &a_resumed, &b_ran};

    ASSERT_TRUE(job_dispatch(sys, job_a_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    ASSERT_TRUE(job_dispatch(sys, job_b_fn, &ctx, 0, NULL) != JOB_ID_INVALID);

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
    ASSERT_EQ_INT(1, b_ran);
    ASSERT_EQ_INT(1, a_resumed);

    job_system_shutdown(sys);
    return 0;
}

static int test_work_stealing_makes_progress(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,2, 2048, 64 * 1024, 2048, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    uint32_t worker_hits[2] = {0, 0};
    struct count_ctx ctx = {worker_hits, ARRAY_SIZE(worker_hits)};

    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(job_dispatch(sys, count_job_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    }

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
    ASSERT_EQ_INT(20, (int)(worker_hits[0] + worker_hits[1]));

    job_system_shutdown(sys);
    return 0;
}

static int test_priority_scheduling_respects_order(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 16, 64 * 1024, 2048, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    int order[2] = {0, 0};
    size_t cursor = 0;

    struct priority_ctx low = {order, &cursor, 1};
    struct priority_ctx high = {order, &cursor, 2};

    ASSERT_TRUE(job_dispatch(sys, record_priority_fn, &low, -10, NULL) != JOB_ID_INVALID);
    ASSERT_TRUE(job_dispatch(sys, record_priority_fn, &high, 10, NULL) != JOB_ID_INVALID);

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
    ASSERT_EQ_INT(2, order[0]);
    ASSERT_EQ_INT(1, order[1]);

    job_system_shutdown(sys);
    return 0;
}

static int build_trace(char *out_buffer) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 32, 64 * 1024, 2048, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    size_t cursor = 0;
    struct record_ctx ctx = {out_buffer, &cursor};

    ASSERT_TRUE(job_dispatch(sys, record_trace_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    ASSERT_TRUE(job_dispatch(sys, sibling_trace_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));

    job_system_shutdown(sys);
    return atomic_load(&g_failure_flag);
}

static int test_deterministic_single_thread_mode(void) {
    char trace_a[128] = {0};
    char trace_b[128] = {0};

    ASSERT_EQ_INT(0, build_trace(trace_a));
    ASSERT_EQ_INT(0, build_trace(trace_b));

    ASSERT_EQ_STR(trace_a, trace_b);
    return 0;
}

static int test_wait_on_satisfied_counter_is_immediate(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 32, 64 * 1024, 2048, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_counter_t counter;
    job_counter_init(&counter, 0);

    int waited = 0;
    struct wait_job_ctx ctx = {&counter, &waited};

    ASSERT_TRUE(job_dispatch(sys, wait_immediate_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
    ASSERT_EQ_INT(1, waited);

    job_system_shutdown(sys);
    return 0;
}

static int test_counter_underflow_protected(void) {
    job_counter_t counter;
    job_counter_init(&counter, 0);
    ASSERT_EQ_INT(-1, job_counter_dec(&counter));
    ASSERT_EQ_UINT(0, job_counter_value(&counter));
    return 0;
}

static int test_reject_too_small_stack_size(void) {
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 8, 1024, 2048, 1);
    ASSERT_TRUE(sys_create_status == JOB_CREATE_ERR_INVALID);
    return 0;
}

static int test_queue_wraparound_preserves_order(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 32, 64 * 1024, 2048, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    int order[32] = {0};
    size_t cursor = 0;
    struct enqueue_ctx ctx = {order, &cursor};

    for (int i = 0; i < 24; ++i) {
        order[i] = i;
        job_id_t id = job_dispatch(sys, enqueue_job_fn, &ctx, 0, NULL);
        if (id == JOB_ID_INVALID) {
            fprintf(stderr, "dispatch failed at %d\n", i);
            TEST_FAIL("%s", "dispatch failed");
        }
    }

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
    for (int i = 0; i < 24; ++i) {
        ASSERT_EQ_INT(i, order[i]);
    }

    job_system_shutdown(sys);
    return 0;
}

static int test_shutdown_drains_safely(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,2, 2048, 64 * 1024, 2048, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    atomic_int completed;
    atomic_init(&completed, 0);
    struct slow_ctx ctx = {&completed};

    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(job_dispatch(sys, slow_job_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    }

    job_system_shutdown(sys);
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
    ASSERT_EQ_INT(10, atomic_load_explicit(&completed, memory_order_relaxed));
    return 0;
}

static int test_invalid_dispatch_arguments_fail(void) {
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 8, 64 * 1024, 2048,1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_id_t bad = job_dispatch(sys, NULL, NULL, 0, NULL);
    ASSERT_EQ_UINT(JOB_ID_INVALID, bad);

    job_system_shutdown(sys);
    return 0;
}

static int test_queue_capacity_exhaustion_is_explicit(void) {
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 1, 64 * 1024, 2048, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    ASSERT_TRUE(job_dispatch(sys, noop_fn, NULL, 0, NULL) != JOB_ID_INVALID);
    job_id_t overflow = job_dispatch(sys, noop_fn, NULL, 0, NULL);
    ASSERT_EQ_UINT(JOB_ID_INVALID, overflow);

    job_system_shutdown(sys);
    return 0;
}

static int test_double_wait_and_signal_are_safe(void) {
    job_counter_t counter;
    job_counter_init(&counter, 0);

    ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(&counter, 0));
    ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(&counter, 0));
    ASSERT_EQ_INT(-1, job_counter_dec(&counter));

    return 0;
}

static int test_no_double_execution_after_steal(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,2, 2048, 64 * 1024, 2048,0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    busy_wait_ms(1);

    atomic_int hits;
    atomic_init(&hits, 0);
    struct once_ctx ctx = {&hits};

    for (int i = 0; i < 50; ++i) {
        ASSERT_TRUE(job_dispatch(sys, job_once_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    }

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
    ASSERT_EQ_INT(50, atomic_load_explicit(&hits, memory_order_relaxed));

    job_system_shutdown(sys);
    return 0;
}

static int test_no_lost_wakeups(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,2, 2048, 64 * 1024, 2048, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_counter_t counter;
    job_counter_init(&counter, 1);

    volatile int resumed = 0;
    struct wait_signal_ctx ctx = {&counter, &resumed};

    ASSERT_TRUE(job_dispatch(sys, wait_job_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    ASSERT_TRUE(job_dispatch(sys, signal_job_fn, &ctx, 0, NULL) != JOB_ID_INVALID);

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
    ASSERT_EQ_INT(1, resumed);

    job_system_shutdown(sys);
    return 0;
}

static int test_no_counter_wraparound(void) {
    job_counter_t counter;
    job_counter_init(&counter, 0);
    for (int i = 0; i < 1000; ++i) {
        ASSERT_EQ_INT(0, job_counter_add(&counter, 1));
        ASSERT_EQ_INT(0, job_counter_dec(&counter));
    }
    ASSERT_EQ_UINT(0, job_counter_value(&counter));
    return 0;
}

static int test_no_resume_after_complete(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 8, 64 * 1024, 2048, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    atomic_int runs;
    atomic_init(&runs, 0);
    struct once_ctx ctx = {&runs};

    ASSERT_TRUE(job_dispatch(sys, job_once_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
    ASSERT_EQ_INT(1, atomic_load_explicit(&runs, memory_order_relaxed));

    job_system_shutdown(sys);
    return 0;
}

static int test_deterministic_trace_debug_mode(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 16, 64 * 1024, 2048,1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    char trace[8] = {0};
    size_t cursor = 0;

    struct record_ctx ctx = {trace, &cursor};
    ASSERT_TRUE(job_dispatch(sys, record_trace_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    ASSERT_TRUE(job_dispatch(sys, sibling_trace_fn, &ctx, 0, NULL) != JOB_ID_INVALID);
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));

    char expected[] = {'A', 'C', 'B', '\0'};
    ASSERT_EQ_STR(expected, trace);

    job_system_shutdown(sys);
    return 0;
}

static int test_mini_frame_execution(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,2, 2048, 64 * 1024, 2048, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    for (int frame = 0; frame < 32; ++frame) {
        job_counter_t frame_counter;
        job_counter_init(&frame_counter, 0);

        int slots[10] = {0};
        struct task_slot_ctx ctxs[10];
        for (int i = 0; i < 10; ++i) {
            ctxs[i].slot = &slots[i];
            ASSERT_TRUE(job_dispatch(sys, task_slot_fn, &ctxs[i], 0, &frame_counter) != JOB_ID_INVALID);
        }

        ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(&frame_counter, 0));
        ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
        for (int i = 0; i < 10; ++i) {
            ASSERT_EQ_INT(1, slots[i]);
        }
    }

    job_system_shutdown(sys);
    return 0;
}

static int test_instrumentation_invariants(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,2, 2048, 64 * 1024, 2048, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(job_dispatch(sys, noop_fn, NULL, 0, NULL) != JOB_ID_INVALID);
    }

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(0, atomic_load(&g_failure_flag));
    ASSERT_EQ_UINT(job_system_jobs_started(sys), job_system_jobs_completed(sys));

    job_system_shutdown(sys);
    return 0;
}

static void instr_noop(void *user) { (void)user; }

static int test_instrumentation_toggle_records_events(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,2, 2048, 64 * 1024, 2048, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_instrument_enable(0);
    uint64_t before = job_instrument_count();
    ASSERT_TRUE(job_dispatch(sys, instr_noop, NULL, 0, NULL) != JOB_ID_INVALID);
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_UINT(before, job_instrument_count());

    job_instrument_enable(1);
    before = job_instrument_count();
    ASSERT_TRUE(job_dispatch(sys, instr_noop, NULL, 0, NULL) != JOB_ID_INVALID);
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_TRUE(job_instrument_count() > before);

    job_system_shutdown(sys);
    return 0;
}

/* --- MPMC queue tests (RED until lock-free queue is implemented) --- */
struct producer_ctx {
    job_system_t *sys;
    atomic_int *counter;
    int jobs_per_producer;
};

static void inc_fn(void *user) {
    struct producer_ctx *ctx = (struct producer_ctx *)user;
    atomic_fetch_add_explicit(ctx->counter, 1, memory_order_relaxed);
}

static int producer_thread_fn(void *arg) {
    struct producer_ctx *c = (struct producer_ctx *)arg;
    for (int j = 0; j < c->jobs_per_producer; ++j) {
        if (job_dispatch(c->sys, inc_fn, c, 0, NULL) == JOB_ID_INVALID) {
            atomic_store(&g_failure_flag, 1);
            return -1;
        }
    }
    return 0;
}

static int test_mpmc_concurrent_producers_consumers(void) {
    atomic_store(&g_failure_flag, 0);
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,4, 8192, 64 * 1024, 4096, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    atomic_int completed;
    atomic_init(&completed, 0);

    const int producers = 4;
    const int jobs_each = 1000;
    thrd_t threads[producers];
    struct producer_ctx ctx = {sys, &completed, jobs_each};
    for (int i = 0; i < producers; ++i) {
        thrd_create(&threads[i], producer_thread_fn, &ctx);
    }

    for (int i = 0; i < producers; ++i) {
        thrd_join(threads[i], NULL);
    }

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(producers * jobs_each, atomic_load_explicit(&completed, memory_order_relaxed));
    ASSERT_EQ_UINT(job_system_jobs_started(sys), job_system_jobs_completed(sys));

    job_system_shutdown(sys);
    return 0;
}

static int test_queue_reports_lock_free(void) {
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,2, 2048, 64 * 1024, 2048, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    /* Probe must report lock-free queue semantics. */
    ASSERT_EQ_INT(1, job_system_queue_is_lock_free(sys));

    job_system_shutdown(sys);
    return 0;
}

static struct test_case TESTS[] = {
    {"fiber_yield_resume_ordering", test_fiber_yield_resume_ordering},
    {"fan_out_fan_in_counter", test_fan_out_fan_in_counter},
    {"wait_parks_fiber_not_worker", test_wait_parks_fiber_not_worker},
    {"work_stealing_makes_progress", test_work_stealing_makes_progress},
    {"randomized_dispatch_yield_interleavings", test_randomized_dispatch_yield_interleavings},
    {"priority_scheduling_respects_order", test_priority_scheduling_respects_order},
    {"deterministic_single_thread_mode", test_deterministic_single_thread_mode},
    {"wait_on_satisfied_counter_is_immediate", test_wait_on_satisfied_counter_is_immediate},
    {"counter_underflow_protected", test_counter_underflow_protected},
    {"reject_too_small_stack_size", test_reject_too_small_stack_size},
    {"queue_wraparound_preserves_order", test_queue_wraparound_preserves_order},
    {"shutdown_drains_safely", test_shutdown_drains_safely},
    {"invalid_dispatch_arguments_fail", test_invalid_dispatch_arguments_fail},
    {"queue_capacity_exhaustion_is_explicit", test_queue_capacity_exhaustion_is_explicit},
    {"double_wait_and_signal_are_safe", test_double_wait_and_signal_are_safe},
    {"no_double_execution_after_steal", test_no_double_execution_after_steal},
    {"no_lost_wakeups", test_no_lost_wakeups},
    {"no_counter_wraparound", test_no_counter_wraparound},
    {"no_resume_after_complete", test_no_resume_after_complete},
    {"deterministic_trace_debug_mode", test_deterministic_trace_debug_mode},
    {"mini_frame_execution", test_mini_frame_execution},
    {"instrumentation_invariants", test_instrumentation_invariants},
    {"instrumentation_toggle_records_events", test_instrumentation_toggle_records_events},
    {"mpmc_concurrent_producers_consumers", test_mpmc_concurrent_producers_consumers},
    {"queue_reports_lock_free", test_queue_reports_lock_free},
};

int main(void) {
    const char *only = getenv("P000_TEST_ONLY");
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        if (only && only[0] != '\0' && strcmp(only, tc->name) != 0) {
            continue;
        }
        printf("RUN %s\n", tc->name);
        fflush(stdout);
        int rc = tc->fn();
        if (rc == 0) {
            passed++;
            printf("OK %s\n", tc->name);
        } else {
            fprintf(stderr, "Test failed: %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }

    if (only && only[0] != '\0') {
        return (passed == 1) ? 0 : 1;
    }

    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }

    return 1;
}
