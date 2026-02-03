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
        return 1;                                                                                   \
    } while (0)

#define ASSERT_TRUE(cond)                                                                           \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            TEST_FAIL("%s", #cond);                                                                 \
        }                                                                                           \
    } while (0)

#define ASSERT_EQ_U64(expected, actual)                                                             \
    do {                                                                                            \
        unsigned long long _exp = (unsigned long long)(expected);                                   \
        unsigned long long _act = (unsigned long long)(actual);                                     \
        if (_exp != _act) {                                                                         \
            TEST_FAIL("expected %llu got %llu", _exp, _act);                                       \
        }                                                                                           \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void yield_once_job(void *user) {
    int *state = (int *)user;
    if (*state == 0) {
        *state = 1;
        job_yield();
    }
}

static int snapshot_is_all_zero(const job_queue_diag_snapshot_t *s) {
    return s->enqueue_calls == 0 && s->enqueue_scanned_slots == 0 && s->enqueue_claim_fail == 0 && s->enqueue_success == 0 &&
           s->pop_calls == 0 && s->pop_scanned_slots == 0 && s->pop_ready_seen == 0 && s->pop_claim_fail == 0 && s->pop_success == 0 &&
           s->cond_waits == 0;
}

static int test_queue_diagnostics_snapshot_and_reset(void) {
    job_system_t sys_storage;
    job_system_t *sys = &sys_storage;

    job_system_create_status_t st = job_system_create(sys, 2, 256, 64 * 1024, 256, 0);
    ASSERT_TRUE(st == JOB_CREATE_OK);
    ASSERT_TRUE(job_system_start(sys) == 0);

    int states[32] = {0};
    for (size_t i = 0; i < ARRAY_SIZE(states); ++i) {
        job_id_t id = job_dispatch(sys, yield_once_job, &states[i], 0, NULL);
        ASSERT_TRUE(id != JOB_ID_INVALID);
    }
    ASSERT_TRUE(job_system_wait_idle(sys) == 0);

    job_queue_diag_snapshot_t snap;
    memset(&snap, 0xAB, sizeof(snap));
    job_system_queue_diag_snapshot(sys, &snap);

    if (job_system_queue_diag_supported()) {
        ASSERT_TRUE(snap.enqueue_calls > 0);
        ASSERT_TRUE(snap.enqueue_success > 0);
        ASSERT_TRUE(snap.pop_calls > 0);
        ASSERT_TRUE(snap.pop_success > 0);
        ASSERT_TRUE(snap.enqueue_scanned_slots >= snap.enqueue_calls);
        ASSERT_TRUE(snap.pop_scanned_slots >= snap.pop_calls);
    } else {
        ASSERT_TRUE(snapshot_is_all_zero(&snap));
    }

    job_system_queue_diag_reset(sys);
    memset(&snap, 0xCD, sizeof(snap));
    job_system_queue_diag_snapshot(sys, &snap);
    ASSERT_TRUE(snapshot_is_all_zero(&snap));

    job_system_shutdown(sys);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

int main(void) {
    struct test_case tests[] = {
        {"queue_diagnostics_snapshot_and_reset", test_queue_diagnostics_snapshot_and_reset},
    };

    for (size_t i = 0; i < ARRAY_SIZE(tests); ++i) {
        int rc = tests[i].fn();
        if (rc != 0) {
            fprintf(stderr, "Test '%s' failed\n", tests[i].name);
            return 1;
        }
        printf("PASS %s\n", tests[i].name);
    }

    printf("ALL TESTS PASSED (%zu tests)\n", ARRAY_SIZE(tests));
    return 0;
}
