#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ferrum/ferrum.h"

#define ASSERT_TRUE(cond)                                                                      \
    do {                                                                                       \
        if (!(cond)) {                                                                        \
            fprintf(stderr, "ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            return 1;                                                                         \
        }                                                                                      \
    } while (0)

#define ASSERT_EQ_INT(expected, actual)                                                        \
    do {                                                                                       \
        int64_t _exp = (int64_t)(expected);                                                    \
        int64_t _act = (int64_t)(actual);                                                      \
        if (_exp != _act) {                                                                    \
            fprintf(stderr, "ASSERT_EQ_INT failed at %s:%d: expected %lld got %lld\n",         \
                    __FILE__, __LINE__, (long long)_exp, (long long)_act);                     \
            return 1;                                                                         \
        }                                                                                      \
    } while (0)

#define ASSERT_EQ_UINT(expected, actual)                                                       \
    do {                                                                                       \
        uint64_t _exp = (uint64_t)(expected);                                                  \
        uint64_t _act = (uint64_t)(actual);                                                    \
        if (_exp != _act) {                                                                    \
            fprintf(stderr, "ASSERT_EQ_UINT failed at %s:%d: expected %llu got %llu\n",       \
                    __FILE__, __LINE__, (unsigned long long)_exp, (unsigned long long)_act);   \
            return 1;                                                                         \
        }                                                                                      \
    } while (0)

#define ASSERT_EQ_STR(expected, actual)                                                        \
    do {                                                                                       \
        if (strcmp((expected), (actual)) != 0) {                                               \
            fprintf(stderr, "ASSERT_EQ_STR failed at %s:%d: expected '%s' got '%s'\n",        \
                    __FILE__, __LINE__, (expected), (actual));                                 \
            return 1;                                                                         \
        }                                                                                      \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

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

static int test_fiber_yield_resume_ordering(void) {
    job_system_t *sys = job_system_create(1, 8, 64 * 1024, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    int trace[3] = {0, 0, 0};
    size_t cursor = 0;

    struct yield_payload {
        int *trace;
        size_t *cursor;
    } payload = {trace, &cursor};

    job_counter_t counter;
    job_counter_init(&counter, 0);

    void yield_job(void *user) {
        struct yield_payload *p = (struct yield_payload *)user;
        p->trace[(*p->cursor)++] = 1;
        job_yield();
        p->trace[(*p->cursor)++] = 2;
    }

    void other_job(void *user) {
        (void)user;
        trace[cursor++] = 3;
    }

    ASSERT_TRUE(job_dispatch(sys, yield_job, &payload, 0, &counter) != JOB_ID_INVALID);
    ASSERT_TRUE(job_dispatch(sys, other_job, NULL, 0, &counter) != JOB_ID_INVALID);

    ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(&counter, 0));
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));

    ASSERT_EQ_INT(3, (int)cursor);
    ASSERT_EQ_INT(1, trace[0]);
    ASSERT_EQ_INT(3, trace[1]);
    ASSERT_EQ_INT(2, trace[2]);

    job_system_shutdown(sys);
    return 0;
}

static int test_fan_out_fan_in_counter(void) {
    job_system_t *sys = job_system_create(2, 128, 64 * 1024, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_counter_t counter;
    job_counter_init(&counter, 0);

    struct fan_state {
        job_counter_t *counter;
        int *child_runs;
    } state = {&counter, NULL};

    int child_runs[100] = {0};
    state.child_runs = child_runs;

    void child_job(void *user) {
        int *slot = (int *)user;
        (*slot)++;
    }

    void parent_job(void *user) {
        struct fan_state *s = (struct fan_state *)user;
        for (size_t i = 0; i < 100; ++i) {
            ASSERT_TRUE(job_dispatch(sys, child_job, &s->child_runs[i], 0, s->counter) != JOB_ID_INVALID);
        }
        ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(s->counter, 0));
    }

    ASSERT_TRUE(job_dispatch(sys, parent_job, &state, 0, &counter) != JOB_ID_INVALID);
    ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(&counter, 0));
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));

    for (size_t i = 0; i < 100; ++i) {
        ASSERT_EQ_INT(1, child_runs[i]);
    }

    job_system_shutdown(sys);
    return 0;
}

static int test_wait_parks_fiber_not_worker(void) {
    job_system_t *sys = job_system_create(1, 16, 64 * 1024, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_counter_t counter;
    job_counter_init(&counter, 1);

    int b_ran = 0;
    int a_resumed = 0;

    void job_a(void *user) {
        (void)user;
        job_wait_status_t st = job_wait_counter(&counter, 0);
        ASSERT_EQ_INT(JOB_WAIT_OK, st);
        a_resumed = 1;
    }

    void job_b(void *user) {
        (void)user;
        b_ran = 1;
        ASSERT_EQ_INT(0, job_counter_dec(&counter));
    }

    ASSERT_TRUE(job_dispatch(sys, job_a, NULL, 0, &counter) != JOB_ID_INVALID);
    ASSERT_TRUE(job_dispatch(sys, job_b, NULL, 0, &counter) != JOB_ID_INVALID);

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(1, b_ran);
    ASSERT_EQ_INT(1, a_resumed);

    job_system_shutdown(sys);
    return 0;
}

static int test_work_stealing_makes_progress(void) {
    job_system_t *sys = job_system_create(2, 64, 64 * 1024, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    uint32_t worker_hits[2] = {0, 0};

    void count_job(void *user) {
        uint32_t wid = job_current_worker_id();
        if (wid < ARRAY_SIZE(worker_hits)) {
            worker_hits[wid]++;
        }
    }

    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(job_dispatch(sys, count_job, NULL, 0, NULL) != JOB_ID_INVALID);
    }

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_TRUE(worker_hits[0] > 0);
    ASSERT_TRUE(worker_hits[1] > 0);

    job_system_shutdown(sys);
    return 0;
}

static int test_priority_scheduling_respects_order(void) {
    job_system_t *sys = job_system_create(1, 16, 64 * 1024, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    int order[2] = {0, 0};
    size_t cursor = 0;

    void low_job(void *user) {
        (void)user;
        order[cursor++] = 1;
    }

    void high_job(void *user) {
        (void)user;
        order[cursor++] = 2;
    }

    ASSERT_TRUE(job_dispatch(sys, low_job, NULL, -10, NULL) != JOB_ID_INVALID);
    ASSERT_TRUE(job_dispatch(sys, high_job, NULL, 10, NULL) != JOB_ID_INVALID);

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(2, order[0]);
    ASSERT_EQ_INT(1, order[1]);

    job_system_shutdown(sys);
    return 0;
}

static int test_deterministic_single_thread_mode(void) {
    char trace_a[128] = {0};
    char trace_b[128] = {0};

    void build_trace(char *out_buffer) {
        job_system_t *sys = job_system_create(1, 32, 64 * 1024, 1);
        ASSERT_TRUE(sys != NULL);
        ASSERT_EQ_INT(0, job_system_start(sys));

        size_t cursor = 0;
        void record_job(void *user) {
            char *buf = (char *)user;
            buf[cursor++] = 'A';
            job_yield();
            buf[cursor++] = 'B';
        }

        void sibling_job(void *user) {
            char *buf = (char *)user;
            buf[cursor++] = 'C';
        }

        ASSERT_TRUE(job_dispatch(sys, record_job, out_buffer, 0, NULL) != JOB_ID_INVALID);
        ASSERT_TRUE(job_dispatch(sys, sibling_job, out_buffer, 0, NULL) != JOB_ID_INVALID);
        ASSERT_EQ_INT(0, job_system_wait_idle(sys));
        job_system_shutdown(sys);
    }

    build_trace(trace_a);
    build_trace(trace_b);

    ASSERT_EQ_STR(trace_a, trace_b);
    return 0;
}

static int test_wait_on_satisfied_counter_is_immediate(void) {
    job_system_t *sys = job_system_create(1, 8, 64 * 1024, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_counter_t counter;
    job_counter_init(&counter, 0);

    int waited = 0;
    void wait_job(void *user) {
        (void)user;
        job_wait_status_t st = job_wait_counter(&counter, 0);
        ASSERT_EQ_INT(JOB_WAIT_OK, st);
        waited = 1;
    }

    ASSERT_TRUE(job_dispatch(sys, wait_job, NULL, 0, NULL) != JOB_ID_INVALID);
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
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
    job_system_t *sys = job_system_create(1, 8, 1024, 1);
    ASSERT_TRUE(sys == NULL);
    return 0;
}

static int test_queue_wraparound_preserves_order(void) {
    job_system_t *sys = job_system_create(1, 8, 64 * 1024, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    int order[32] = {0};
    size_t cursor = 0;

    void enqueue_job(void *user) {
        int value = *(int *)user;
        order[cursor++] = value;
    }

    for (int i = 0; i < 24; ++i) {
        int *payload = (int *)malloc(sizeof(int));
        ASSERT_TRUE(payload != NULL);
        *payload = i;
        ASSERT_TRUE(job_dispatch(sys, enqueue_job, payload, 0, NULL) != JOB_ID_INVALID);
    }

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    for (int i = 0; i < 24; ++i) {
        ASSERT_EQ_INT(i, order[i]);
    }

    job_system_shutdown(sys);
    return 0;
}

static int test_shutdown_drains_safely(void) {
    job_system_t *sys = job_system_create(2, 32, 64 * 1024, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    volatile int completed = 0;

    void slow_job(void *user) {
        (void)user;
        busy_wait_ms(5);
        completed++;
    }

    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(job_dispatch(sys, slow_job, NULL, 0, NULL) != JOB_ID_INVALID);
    }

    job_system_shutdown(sys);
    ASSERT_EQ_INT(10, completed);
    return 0;
}

static int test_invalid_dispatch_arguments_fail(void) {
    job_system_t *sys = job_system_create(1, 8, 64 * 1024, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_id_t bad = job_dispatch(sys, NULL, NULL, 0, NULL);
    ASSERT_EQ_UINT(JOB_ID_INVALID, bad);

    job_system_shutdown(sys);
    return 0;
}

static int test_queue_capacity_exhaustion_is_explicit(void) {
    job_system_t *sys = job_system_create(1, 1, 64 * 1024, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    void noop(void *user) {
        (void)user;
    }

    ASSERT_TRUE(job_dispatch(sys, noop, NULL, 0, NULL) != JOB_ID_INVALID);
    job_id_t overflow = job_dispatch(sys, noop, NULL, 0, NULL);
    ASSERT_EQ_UINT(JOB_ID_INVALID, overflow);

    job_system_shutdown(sys);
    return 0;
}

static int test_double_wait_and_signal_are_safe(void) {
    job_system_t *sys = job_system_create(1, 8, 64 * 1024, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_counter_t counter;
    job_counter_init(&counter, 0);

    ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(&counter, 0));
    ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(&counter, 0));
    ASSERT_EQ_INT(-1, job_counter_dec(&counter));

    job_system_shutdown(sys);
    return 0;
}

static int test_no_double_execution_after_steal(void) {
    job_system_t *sys = job_system_create(2, 64, 64 * 1024, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    volatile int hits = 0;
    void once_job(void *user) {
        (void)user;
        hits++;
    }

    for (int i = 0; i < 50; ++i) {
        ASSERT_TRUE(job_dispatch(sys, once_job, NULL, 0, NULL) != JOB_ID_INVALID);
    }

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(50, hits);

    job_system_shutdown(sys);
    return 0;
}

static int test_no_lost_wakeups(void) {
    job_system_t *sys = job_system_create(2, 32, 64 * 1024, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    job_counter_t counter;
    job_counter_init(&counter, 1);

    volatile int resumed = 0;

    void wait_job(void *user) {
        (void)user;
        ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(&counter, 0));
        resumed = 1;
    }

    void signal_job(void *user) {
        (void)user;
        busy_wait_ms(1);
        ASSERT_EQ_INT(0, job_counter_dec(&counter));
    }

    ASSERT_TRUE(job_dispatch(sys, wait_job, NULL, 0, NULL) != JOB_ID_INVALID);
    ASSERT_TRUE(job_dispatch(sys, signal_job, NULL, 0, NULL) != JOB_ID_INVALID);

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
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
    job_system_t *sys = job_system_create(1, 8, 64 * 1024, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    volatile int runs = 0;

    void job_once(void *user) {
        (void)user;
        runs++;
    }

    ASSERT_TRUE(job_dispatch(sys, job_once, NULL, 0, NULL) != JOB_ID_INVALID);
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_INT(1, runs);

    job_system_shutdown(sys);
    return 0;
}

static int test_deterministic_trace_debug_mode(void) {
    job_system_t *sys = job_system_create(1, 16, 64 * 1024, 1);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    char trace[8] = {0};
    size_t cursor = 0;

    void record(void *user) {
        char *buf = (char *)user;
        buf[cursor++] = 'X';
        job_yield();
        buf[cursor++] = 'Y';
    }

    ASSERT_TRUE(job_dispatch(sys, record, trace, 0, NULL) != JOB_ID_INVALID);
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));

    char expected[] = {'X', 'Y', '\0'};
    ASSERT_EQ_STR(expected, trace);

    job_system_shutdown(sys);
    return 0;
}

static int test_mini_frame_execution(void) {
    job_system_t *sys = job_system_create(2, 64, 64 * 1024, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    for (int frame = 0; frame < 32; ++frame) {
        job_counter_t frame_counter;
        job_counter_init(&frame_counter, 0);

        void task(void *user) {
            int *slot = (int *)user;
            (*slot)++;
        }

        int slots[10] = {0};
        for (int i = 0; i < 10; ++i) {
            ASSERT_TRUE(job_dispatch(sys, task, &slots[i], 0, &frame_counter) != JOB_ID_INVALID);
        }

        ASSERT_EQ_INT(JOB_WAIT_OK, job_wait_counter(&frame_counter, 0));
        for (int i = 0; i < 10; ++i) {
            ASSERT_EQ_INT(1, slots[i]);
        }
    }

    job_system_shutdown(sys);
    return 0;
}

static int test_instrumentation_invariants(void) {
    job_system_t *sys = job_system_create(2, 32, 64 * 1024, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_EQ_INT(0, job_system_start(sys));

    void noop(void *user) { (void)user; }

    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(job_dispatch(sys, noop, NULL, 0, NULL) != JOB_ID_INVALID);
    }

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_EQ_UINT(job_system_jobs_started(sys), job_system_jobs_completed(sys));

    job_system_shutdown(sys);
    return 0;
}

static struct test_case TESTS[] = {
    {"fiber_yield_resume_ordering", test_fiber_yield_resume_ordering},
    {"fan_out_fan_in_counter", test_fan_out_fan_in_counter},
    {"wait_parks_fiber_not_worker", test_wait_parks_fiber_not_worker},
    {"work_stealing_makes_progress", test_work_stealing_makes_progress},
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
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        int rc = tc->fn();
        if (rc == 0) {
            passed++;
        } else {
            fprintf(stderr, "Test failed: %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }

    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }

    return 1;
}
