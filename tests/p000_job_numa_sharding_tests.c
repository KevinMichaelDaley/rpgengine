#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>

#include "ferrum/ferrum.h"

static atomic_int g_fail = 0;
#define ASSERT_TRUE(x) do { if (!(x)) { fprintf(stderr, "ASSERT_TRUE failed: %s @ %s:%d\n", #x, __FILE__, __LINE__); atomic_store(&g_fail, 1); return 1; } } while(0)
#define ASSERT_EQ_INT(a,b) do { int _aa=(a), _bb=(b); if (_aa!=_bb){ fprintf(stderr, "ASSERT_EQ_INT %d != %d @ %s:%d\n", _aa, _bb, __FILE__, __LINE__); atomic_store(&g_fail, 1); return 1; } } while(0)

struct count_ctx {
    atomic_uint *per_node;
    uint32_t node_count;
};

static void count_job(void *user) {
    struct count_ctx *ctx = (struct count_ctx *)user;
    uint32_t node = job_current_worker_node();
    if (node >= ctx->node_count) {
        atomic_store(&g_fail, 1);
        return;
    }
    atomic_fetch_add_explicit(&ctx->per_node[node], 1u, memory_order_relaxed);
}

static int test_enable_and_bias(void) {
    atomic_store(&g_fail, 0);
    job_system_t sys_; job_system_t *sys = &sys_;
    ASSERT_EQ_INT(JOB_CREATE_OK, job_system_create(sys, 4, 2048, 64*1024, 512, 0));

    /* Enable NUMA with 2 nodes (simulated). */
    ASSERT_EQ_INT(0, job_system_enable_numa(sys, 2));
    ASSERT_EQ_INT(1, job_system_numa_enabled(sys));

    ASSERT_EQ_INT(0, job_system_start(sys));

    atomic_uint per_node[2];
    atomic_init(&per_node[0], 0u);
    atomic_init(&per_node[1], 0u);

    struct count_ctx ctx = { per_node, 2 };
    job_counter_t c; job_counter_init(&c, 0);

    /* Dispatch a batch; expect strong bias towards local node buckets. */
    for (int i = 0; i < 200; ++i) {
        ASSERT_TRUE(job_dispatch(sys, count_job, &ctx, 0, &c) != JOB_ID_INVALID);
    }
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));

    uint32_t n0 = atomic_load_explicit(&per_node[0], memory_order_relaxed);
    uint32_t n1 = atomic_load_explicit(&per_node[1], memory_order_relaxed);
    /* Both nodes should have processed some work; bias means neither is starved. */
    ASSERT_TRUE(n0 > 0 && n1 > 0);

    job_system_shutdown(sys);
    return atomic_load(&g_fail);
}

static int test_inter_node_steal(void) {
    atomic_store(&g_fail, 0);
    job_system_t sys_; job_system_t *sys = &sys_;
    ASSERT_EQ_INT(JOB_CREATE_OK, job_system_create(sys, 2, 2048, 64*1024, 128, 0));
    ASSERT_EQ_INT(0, job_system_enable_numa(sys, 2));
    ASSERT_EQ_INT(0, job_system_start(sys));

    atomic_uint per_node[2];
    atomic_init(&per_node[0], 0u);
    atomic_init(&per_node[1], 0u);
    struct count_ctx ctx = { per_node, 2 };
    job_counter_t c; job_counter_init(&c, 0);

    /* Fill enough jobs that one node will need to steal to finish. */
    for (int i = 0; i < 200; ++i) {
        ASSERT_TRUE(job_dispatch(sys, count_job, &ctx, 0, &c) != JOB_ID_INVALID);
    }
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));

    uint32_t total = atomic_load(&per_node[0]) + atomic_load(&per_node[1]);
    ASSERT_EQ_INT(200, (int)total);

    job_system_shutdown(sys);
    return atomic_load(&g_fail);
}

int main(void) {
    int rc = 0;
    fprintf(stderr, "RUN numa_enable_and_bias\n");
    rc |= test_enable_and_bias();
    fprintf(stderr, rc ? "FAIL numa_enable_and_bias\n" : "OK numa_enable_and_bias\n");

    fprintf(stderr, "RUN numa_inter_node_steal\n");
    rc |= test_inter_node_steal();
    fprintf(stderr, rc ? "FAIL numa_inter_node_steal\n" : "OK numa_inter_node_steal\n");

    if (!rc) fprintf(stderr, "All 2 tests passed\n");
    return rc;
}
