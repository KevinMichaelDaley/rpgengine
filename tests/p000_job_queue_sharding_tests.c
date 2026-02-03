#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/ferrum.h"

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

static atomic_uint g_counts[64];

static void count_job(void *user) {
    (void)user;
    uint32_t wid = job_current_worker_id();
    if (wid < 64u) {
        atomic_fetch_add(&g_counts[wid], 1);
    }
}

static int test_sharded_queue_enabled(void) {
    job_system_t sys_; job_system_t *sys = &sys_;
    job_system_create_status_t st = job_system_create(sys, 2, 2048, 64 * 1024, 2048, 0);
    ASSERT_INT_EQ(JOB_CREATE_OK, st);
    ASSERT_INT_EQ(0, job_system_start(sys));
    ASSERT_INT_EQ(1, job_system_queue_is_sharded(sys));
    job_system_shutdown(sys);
    return 0;
}

static int test_work_stealing_observed(void) {
    for (int i = 0; i < 64; ++i) atomic_store(&g_counts[i], 0);
    job_system_t sys_; job_system_t *sys = &sys_;
    job_system_create_status_t st = job_system_create(sys, 2, 2048, 64 * 1024, 2048, 0);
    ASSERT_INT_EQ(JOB_CREATE_OK, st);
    ASSERT_INT_EQ(0, job_system_start(sys));

    job_counter_t counter; job_counter_init(&counter, 0);
    for (int i = 0; i < 100; ++i) {
        job_id_t id = job_dispatch_to(sys, count_job, NULL, 0, &counter, 0u);
        ASSERT_TRUE(id != JOB_ID_INVALID);
    }
    ASSERT_INT_EQ(JOB_WAIT_OK, job_wait_counter(&counter, 0));
    ASSERT_INT_EQ(0, job_system_wait_idle(sys));

    unsigned c0 = atomic_load(&g_counts[0]);
    unsigned c1 = atomic_load(&g_counts[1]);
    ASSERT_TRUE(c0 > 0);
    ASSERT_TRUE(c1 > 0); /* expect stealing to occur */

    job_system_shutdown(sys);
    return 0;
}

static int test_affinity_toggle(void) {
    job_system_t sys_; job_system_t *sys = &sys_;
    job_system_create_status_t st = job_system_create(sys, 2, 2048, 64 * 1024, 2048, 0);
    ASSERT_INT_EQ(JOB_CREATE_OK, st);
    ASSERT_INT_EQ(0, job_system_enable_affinity(sys, 1));
    ASSERT_INT_EQ(1, job_system_affinity_enabled(sys));
    ASSERT_INT_EQ(0, job_system_start(sys));
    job_system_shutdown(sys);
    return 0;
}

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"sharded_queue_enabled", test_sharded_queue_enabled},
    {"work_stealing_observed", test_work_stealing_observed},
    {"affinity_toggle", test_affinity_toggle},
};

int main(void) {
    size_t total = sizeof(TESTS)/sizeof(TESTS[0]);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        int rc = TESTS[i].fn();
        if (rc == 0) { printf("OK %s\n", TESTS[i].name); passed++; }
        else { fprintf(stderr, "FAIL %s rc=%d\n", TESTS[i].name, rc); break; }
    }
    if (passed == total) { printf("All %zu tests passed\n", passed); return 0; }
    return 1;
}
