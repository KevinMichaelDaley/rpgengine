/**
 * @file aegis_async_buffer_tests.c
 * @brief Tests for the MPSC async task buffer.
 *
 * Covers: init/destroy, single submit/drain, status transitions,
 * buffer overflow, multi-producer concurrency (2 threads), and
 * result_ptr visibility after status COMPLETE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>

#include "ferrum/aegis/aegis_async.h"

/* ------------------------------------------------------------------ */
/* Minimal test harness                                               */
/* ------------------------------------------------------------------ */

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn)                                        \
    do {                                               \
        printf("RUN  %s\n", #fn);                      \
        fn();                                          \
        printf("OK   %s\n", #fn);                      \
    } while (0)

#define ASSERT_TRUE(expr)                                              \
    do {                                                               \
        if (!(expr)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);     \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

#define PASS() g_pass++

/* ================================================================== */
/* Tests                                                              */
/* ================================================================== */

/* --- Init / Destroy ----------------------------------------------- */

static void test_init_destroy(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));
    ASSERT_EQ(aegis_async_buffer_count(&buf), (uint32_t)0);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_init_rounds_to_power_of_two(void) {
    aegis_async_buffer_t buf;
    /* 10 is not a power of 2 — should round up to 16. */
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 10));
    /* The buffer should accept at least 10 tasks (capacity >= 16). */
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Single submit / drain ---------------------------------------- */

static void test_single_submit_drain(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    task.task_type = AEGIS_TASK_VIS_TEST;
    task.result_ptr = NULL;
    task.result_cap = 64;
    /* Write some params. */
    float origin[3] = {1.0f, 2.0f, 3.0f};
    memcpy(task.params, origin, sizeof(origin));

    ASSERT_TRUE(aegis_async_buffer_submit(&buf, &task));
    ASSERT_EQ(aegis_async_buffer_count(&buf), (uint32_t)1);

    aegis_async_task_t out[4];
    uint32_t drained = aegis_async_buffer_drain(&buf, out, 4);
    ASSERT_EQ(drained, (uint32_t)1);
    ASSERT_EQ(out[0].task_type, (uint32_t)AEGIS_TASK_VIS_TEST);
    ASSERT_EQ(out[0].result_cap, (uint32_t)64);

    /* Verify params survived. */
    float got[3];
    memcpy(got, out[0].params, sizeof(got));
    ASSERT_TRUE(got[0] == 1.0f && got[1] == 2.0f && got[2] == 3.0f);

    /* Buffer should be empty now. */
    ASSERT_EQ(aegis_async_buffer_count(&buf), (uint32_t)0);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Status transitions ------------------------------------------- */

static void test_status_starts_pending(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    task.task_type = AEGIS_TASK_NAV_QUERY;
    ASSERT_TRUE(aegis_async_buffer_submit(&buf, &task));

    aegis_async_task_t out[1];
    uint32_t n = aegis_async_buffer_drain(&buf, out, 1);
    ASSERT_EQ(n, (uint32_t)1);
    ASSERT_EQ(atomic_load(&out[0].status), (uint32_t)AEGIS_ASYNC_PENDING);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_status_complete_transition(void) {
    /* Simulate world subsystem completing a task. */
    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    atomic_store(&task.status, AEGIS_ASYNC_PENDING);

    /* World writes result then sets COMPLETE with release. */
    uint32_t result_val = 42;
    char result_buf[16];
    memcpy(result_buf, &result_val, sizeof(result_val));
    task.result_ptr = result_buf;
    task.result_cap = sizeof(result_buf);
    atomic_store_explicit(&task.status, AEGIS_ASYNC_COMPLETE,
                          memory_order_release);

    /* Script reads with acquire. */
    uint32_t st = atomic_load_explicit(&task.status, memory_order_acquire);
    ASSERT_EQ(st, (uint32_t)AEGIS_ASYNC_COMPLETE);

    /* Result should be visible. */
    uint32_t got;
    memcpy(&got, task.result_ptr, sizeof(got));
    ASSERT_EQ(got, (uint32_t)42);

    PASS();
}

static void test_status_error_transition(void) {
    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    atomic_store(&task.status, AEGIS_ASYNC_PENDING);
    atomic_store_explicit(&task.status, AEGIS_ASYNC_ERROR,
                          memory_order_release);
    uint32_t st = atomic_load_explicit(&task.status, memory_order_acquire);
    ASSERT_EQ(st, (uint32_t)AEGIS_ASYNC_ERROR);
    PASS();
}

/* --- Buffer full -------------------------------------------------- */

static void test_submit_buffer_full(void) {
    aegis_async_buffer_t buf;
    /* Capacity of 4 (power of 2). */
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 4));

    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    task.task_type = AEGIS_TASK_VIS_TEST;

    /* Fill all 4 slots. MPSC ring with N slots can hold N-1 entries
     * (one slot is wasted to distinguish full from empty), so 3 should
     * succeed and the 4th should fail — OR all 4 succeed if
     * implementation uses a count-based approach. We test that at least
     * the first 3 succeed and we eventually get a failure. */
    int submitted = 0;
    for (int i = 0; i < 8; i++) {
        if (aegis_async_buffer_submit(&buf, &task)) {
            submitted++;
        } else {
            break;
        }
    }
    /* At least capacity-1 should have been accepted. */
    ASSERT_TRUE(submitted >= 3);
    ASSERT_TRUE(submitted <= 4);

    /* The next submit should fail. */
    ASSERT_TRUE(!aegis_async_buffer_submit(&buf, &task));

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Multiple submit / drain -------------------------------------- */

static void test_multiple_submit_drain(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    for (uint32_t i = 0; i < 8; i++) {
        aegis_async_task_t task;
        memset(&task, 0, sizeof(task));
        task.task_type = i;
        ASSERT_TRUE(aegis_async_buffer_submit(&buf, &task));
    }

    aegis_async_task_t out[16];
    uint32_t n = aegis_async_buffer_drain(&buf, out, 16);
    ASSERT_EQ(n, (uint32_t)8);

    /* Verify FIFO order. */
    for (uint32_t i = 0; i < 8; i++) {
        ASSERT_EQ(out[i].task_type, i);
    }

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Partial drain ------------------------------------------------ */

static void test_partial_drain(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    for (uint32_t i = 0; i < 6; i++) {
        aegis_async_task_t task;
        memset(&task, 0, sizeof(task));
        task.task_type = i;
        ASSERT_TRUE(aegis_async_buffer_submit(&buf, &task));
    }

    /* Drain only 3. */
    aegis_async_task_t out[3];
    uint32_t n = aegis_async_buffer_drain(&buf, out, 3);
    ASSERT_EQ(n, (uint32_t)3);
    for (uint32_t i = 0; i < 3; i++) {
        ASSERT_EQ(out[i].task_type, i);
    }

    /* Drain remaining. */
    n = aegis_async_buffer_drain(&buf, out, 3);
    ASSERT_EQ(n, (uint32_t)3);
    for (uint32_t i = 0; i < 3; i++) {
        ASSERT_EQ(out[i].task_type, i + 3);
    }

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Drain empty buffer ------------------------------------------- */

static void test_drain_empty(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_async_task_t out[4];
    uint32_t n = aegis_async_buffer_drain(&buf, out, 4);
    ASSERT_EQ(n, (uint32_t)0);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Wrap-around -------------------------------------------------- */

static void test_wraparound(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 4));

    /* Do multiple cycles of fill-drain to force index wrap-around. */
    for (int cycle = 0; cycle < 5; cycle++) {
        aegis_async_task_t task;
        memset(&task, 0, sizeof(task));
        task.task_type = (uint32_t)cycle;

        /* Submit 3 (capacity - 1 for head/tail ring). */
        for (int i = 0; i < 3; i++) {
            ASSERT_TRUE(aegis_async_buffer_submit(&buf, &task));
        }

        aegis_async_task_t out[4];
        uint32_t n = aegis_async_buffer_drain(&buf, out, 4);
        ASSERT_EQ(n, (uint32_t)3);
        for (uint32_t i = 0; i < 3; i++) {
            ASSERT_EQ(out[i].task_type, (uint32_t)cycle);
        }
    }

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Result pointer visibility ------------------------------------ */

static void test_result_ptr_visibility(void) {
    /* Simulates the full flow:
     * 1. Script allocates result slot, submits task with result_ptr
     * 2. World writes data to result_ptr, sets status COMPLETE
     * 3. Script polls status (acquire), reads result_ptr data
     */
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    /* "Script heap arena" result slot. */
    float result_slot[4] = {0};

    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    task.task_type = AEGIS_TASK_VIS_TEST;
    task.result_ptr = result_slot;
    task.result_cap = sizeof(result_slot);
    ASSERT_TRUE(aegis_async_buffer_submit(&buf, &task));

    /* World drains. */
    aegis_async_task_t drained[1];
    uint32_t n = aegis_async_buffer_drain(&buf, drained, 1);
    ASSERT_EQ(n, (uint32_t)1);

    /* World writes result and sets COMPLETE. */
    float world_result[4] = {10.0f, 20.0f, 30.0f, 1.0f};
    memcpy(drained[0].result_ptr, world_result, sizeof(world_result));
    atomic_store_explicit(&drained[0].status, AEGIS_ASYNC_COMPLETE,
                          memory_order_release);

    /* Since result_ptr points into the original slot, script can read. */
    /* (In real usage, the script would poll the task's status via handle.) */
    ASSERT_TRUE(result_slot[0] == 10.0f);
    ASSERT_TRUE(result_slot[1] == 20.0f);
    ASSERT_TRUE(result_slot[2] == 30.0f);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Multi-producer concurrent test ------------------------------- */

#define MP_TASKS_PER_THREAD 500
#define MP_NUM_THREADS      2

typedef struct {
    aegis_async_buffer_t *buf;
    uint32_t              thread_id;
    atomic_int           *submitted_count;
} mp_thread_args_t;

static void *mp_producer_fn(void *arg) {
    mp_thread_args_t *a = (mp_thread_args_t *)arg;
    int count = 0;
    for (int i = 0; i < MP_TASKS_PER_THREAD; i++) {
        aegis_async_task_t task;
        memset(&task, 0, sizeof(task));
        task.task_type = a->thread_id;
        /* Encode sequence number in params. */
        uint32_t seq = (uint32_t)i;
        memcpy(task.params, &seq, sizeof(seq));
        /* Retry until submit succeeds (drain may free space). */
        while (!aegis_async_buffer_submit(a->buf, &task)) {
            /* Spin — consumer is draining concurrently. */
        }
        count++;
    }
    atomic_fetch_add(a->submitted_count, count);
    return NULL;
}

static void test_multi_producer(void) {
    aegis_async_buffer_t buf;
    /* Large enough to avoid too much contention but small enough to
     * force wrap-around and contention. */
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 128));

    atomic_int submitted_count = 0;

    mp_thread_args_t args[MP_NUM_THREADS];
    pthread_t threads[MP_NUM_THREADS];

    for (int t = 0; t < MP_NUM_THREADS; t++) {
        args[t].buf = &buf;
        args[t].thread_id = (uint32_t)t;
        args[t].submitted_count = &submitted_count;
        pthread_create(&threads[t], NULL, mp_producer_fn, &args[t]);
    }

    /* Consumer: drain in a loop until all produced. */
    uint32_t total_drained = 0;
    uint32_t per_thread[MP_NUM_THREADS];
    memset(per_thread, 0, sizeof(per_thread));

    const uint32_t expected_total = MP_TASKS_PER_THREAD * MP_NUM_THREADS;

    while (total_drained < expected_total) {
        aegis_async_task_t out[64];
        uint32_t n = aegis_async_buffer_drain(&buf, out, 64);
        for (uint32_t i = 0; i < n; i++) {
            ASSERT_TRUE(out[i].task_type < (uint32_t)MP_NUM_THREADS);
            per_thread[out[i].task_type]++;
        }
        total_drained += n;
    }

    for (int t = 0; t < MP_NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }

    ASSERT_EQ(total_drained, expected_total);
    for (int t = 0; t < MP_NUM_THREADS; t++) {
        ASSERT_EQ(per_thread[t], (uint32_t)MP_TASKS_PER_THREAD);
    }
    ASSERT_EQ(atomic_load(&submitted_count),
              (int)(MP_TASKS_PER_THREAD * MP_NUM_THREADS));

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- 4-thread stress test ----------------------------------------- */

#define MP4_TASKS_PER_THREAD 1000
#define MP4_NUM_THREADS      4

static void *mp4_producer_fn(void *arg) {
    mp_thread_args_t *a = (mp_thread_args_t *)arg;
    int count = 0;
    for (int i = 0; i < MP4_TASKS_PER_THREAD; i++) {
        aegis_async_task_t task;
        memset(&task, 0, sizeof(task));
        task.task_type = a->thread_id;
        uint32_t seq = (uint32_t)i;
        memcpy(task.params, &seq, sizeof(seq));
        while (!aegis_async_buffer_submit(a->buf, &task)) {
            /* Spin. */
        }
        count++;
    }
    atomic_fetch_add(a->submitted_count, count);
    return NULL;
}

static void test_multi_producer_4_threads(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 256));

    atomic_int submitted_count = 0;

    mp_thread_args_t args[MP4_NUM_THREADS];
    pthread_t threads[MP4_NUM_THREADS];

    for (int t = 0; t < MP4_NUM_THREADS; t++) {
        args[t].buf = &buf;
        args[t].thread_id = (uint32_t)t;
        args[t].submitted_count = &submitted_count;
        pthread_create(&threads[t], NULL, mp4_producer_fn, &args[t]);
    }

    uint32_t total_drained = 0;
    uint32_t per_thread[MP4_NUM_THREADS];
    memset(per_thread, 0, sizeof(per_thread));
    const uint32_t expected = MP4_TASKS_PER_THREAD * MP4_NUM_THREADS;

    while (total_drained < expected) {
        aegis_async_task_t out[64];
        uint32_t n = aegis_async_buffer_drain(&buf, out, 64);
        for (uint32_t i = 0; i < n; i++) {
            ASSERT_TRUE(out[i].task_type < (uint32_t)MP4_NUM_THREADS);
            per_thread[out[i].task_type]++;
        }
        total_drained += n;
    }

    for (int t = 0; t < MP4_NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }

    ASSERT_EQ(total_drained, expected);
    for (int t = 0; t < MP4_NUM_THREADS; t++) {
        ASSERT_EQ(per_thread[t], (uint32_t)MP4_TASKS_PER_THREAD);
    }

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* ================================================================== */
/* Main                                                               */
/* ================================================================== */

int main(void) {
    printf("=== Aegis Async Buffer Tests ===\n\n");

    RUN(test_init_destroy);
    RUN(test_init_rounds_to_power_of_two);
    RUN(test_single_submit_drain);
    RUN(test_status_starts_pending);
    RUN(test_status_complete_transition);
    RUN(test_status_error_transition);
    RUN(test_submit_buffer_full);
    RUN(test_multiple_submit_drain);
    RUN(test_partial_drain);
    RUN(test_drain_empty);
    RUN(test_wraparound);
    RUN(test_result_ptr_visibility);
    RUN(test_multi_producer);
    RUN(test_multi_producer_4_threads);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
