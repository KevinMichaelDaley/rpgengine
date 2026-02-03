#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <threads.h>
#include <stdatomic.h>
#include <unistd.h>

#include "ferrum/ferrum.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static vec3_t vec3_cross_local(vec3_t a, vec3_t b) {
    vec3_t r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}

/* Rotate vector by quaternion without building a matrix.
 * v' = v*(s^2 - |u|^2) + 2*u*(u·v) + 2*s*(u × v)
 */
static vec3_t vec3_rotate_by_quat(vec3_t v, quat_t q) {
    vec3_t u = (vec3_t){q.x, q.y, q.z};
    float s = q.w;
    float u_dot_v = (u.x * v.x) + (u.y * v.y) + (u.z * v.z);
    float u_len2 = (u.x * u.x) + (u.y * u.y) + (u.z * u.z);
    vec3_t term1 = (vec3_t){v.x * (s*s - u_len2), v.y * (s*s - u_len2), v.z * (s*s - u_len2)};
    vec3_t term2 = (vec3_t){2.0f * u.x * u_dot_v, 2.0f * u.y * u_dot_v, 2.0f * u.z * u_dot_v};
    vec3_t cx = vec3_cross_local(u, v);
    vec3_t term3 = (vec3_t){2.0f * s * cx.x, 2.0f * s * cx.y, 2.0f * s * cx.z};
    vec3_t out = (vec3_t){term1.x + term2.x + term3.x,
                          term1.y + term2.y + term3.y,
                          term1.z + term2.z + term3.z};
    return out;
}

/* Workloads */
struct perf_ctx {
    vec3_t v;
    quat_t q1, q2, q3;
    volatile float *sinks;
    uint32_t sink_count;
};

static void job_small(void *user) {
    struct perf_ctx *ctx = (struct perf_ctx *)user;
    vec3_t v = ctx->v;
    quat_t q1 = ctx->q1, q2 = ctx->q2, q3 = ctx->q3;
    for (int i = 0; i < 200; ++i) {
        v = vec3_rotate_by_quat(v, q1);
        v = vec3_rotate_by_quat(v, q2);
        v = vec3_rotate_by_quat(v, q3);
    }
    uint32_t wid = job_current_worker_id();
    uint32_t idx = (wid == UINT32_MAX) ? 0u : (wid % ctx->sink_count);
    ctx->sinks[idx] += v.x + v.y + v.z;
}

static void job_medium(void *user) {
    struct perf_ctx *ctx = (struct perf_ctx *)user;
    vec3_t v = ctx->v;
    quat_t q1 = ctx->q1, q2 = ctx->q2, q3 = ctx->q3;
    for (int i = 0; i < 2000; ++i) {
        v = vec3_rotate_by_quat(v, q1);
        v = vec3_rotate_by_quat(v, q2);
        v = vec3_rotate_by_quat(v, q3);
    }
    uint32_t wid = job_current_worker_id();
    uint32_t idx = (wid == UINT32_MAX) ? 0u : (wid % ctx->sink_count);
    ctx->sinks[idx] += v.x + v.y + v.z;
}

static void job_large(void *user) {
    struct perf_ctx *ctx = (struct perf_ctx *)user;
    vec3_t v = ctx->v;
    quat_t q1 = ctx->q1, q2 = ctx->q2, q3 = ctx->q3;
    for (int i = 0; i < 20000; ++i) {
        v = vec3_rotate_by_quat(v, q1);
        v = vec3_rotate_by_quat(v, q2);
        v = vec3_rotate_by_quat(v, q3);
    }
    uint32_t wid = job_current_worker_id();
    uint32_t idx = (wid == UINT32_MAX) ? 0u : (wid % ctx->sink_count);
    ctx->sinks[idx] += v.x + v.y + v.z;
}

static double timespec_ms(const struct timespec *ts) {
    return (double)ts->tv_sec * 1000.0 + (double)ts->tv_nsec / 1e6;
}

static void run_once(uint32_t worker_count) {
    job_system_t sys_; job_system_t *sys = &sys_;
    const uint32_t queue_capacity = 8192;
    const size_t fiber_stack_size = 64 * 1024;
    const size_t fiber_count_max = 8192;

    job_system_create_status_t st = job_system_create(sys, worker_count, queue_capacity,
                                                      fiber_stack_size, fiber_count_max, 0);
    if (st != JOB_CREATE_OK) {
        fprintf(stderr, "SKIP workers=%u create_status=%d\n", worker_count, st);
        return;
    }
    /* Optional CPU affinity toggle via env: JOB_SYS_AFFINITY=1 */
    const char *aff = getenv("JOB_SYS_AFFINITY");
    if (aff && aff[0] != '\0' && aff[0] != '0') {
        (void)job_system_enable_affinity(sys, 1);
    }
    /* Optional NUMA nodes toggle via env: JOB_SYS_NUMA_NODES=N */
    const char *numa_env = getenv("JOB_SYS_NUMA_NODES");
    if (numa_env && numa_env[0] != '\0') {
        char *endp = NULL;
        unsigned long nodes = strtoul(numa_env, &endp, 10);
        if (endp != numa_env && nodes >= 1ul) {
            (void)job_system_enable_numa(sys, (uint32_t)nodes);
        }
    } else {
        (void)job_system_enable_numa_auto(sys);
    }
    if (job_system_start(sys) != 0) {
        fprintf(stderr, "SKIP workers=%u start_failed\n", worker_count);
        return;
    }

    job_counter_t counter;
    job_counter_init(&counter, 0);

    /* Prepare contexts */
    volatile float sinks_local[64] = {0.0f};
    struct perf_ctx ctx = {
        .v = (vec3_t){0.3f, 1.7f, -0.9f},
        .q1 = quat_normalize_safe(quat_from_axis_angle((vec3_t){1,0,0}, 0.7f, 1e-6f), 1e-6f),
        .q2 = quat_normalize_safe(quat_from_axis_angle((vec3_t){0,1,0}, 1.3f, 1e-6f), 1e-6f),
        .q3 = quat_normalize_safe(quat_from_axis_angle((vec3_t){0,0,1}, 0.2f, 1e-6f), 1e-6f),
        .sinks = sinks_local,
        .sink_count = (worker_count < 64u ? worker_count : 64u)
    };

    void (*fns[])(void *) = { job_small, job_medium, job_large };

    /* Timing */
    struct timespec wall_start, wall_end;
    struct timespec cpu_start, cpu_end;
    clock_gettime(CLOCK_MONOTONIC, &wall_start);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_start);

    /* Dispatch 1000 jobs with random mix */
    srand(1234u ^ worker_count);
    for (int i = 0; i < 1000; ++i) {
        int idx = rand() % ARRAY_SIZE(fns);
        if (job_dispatch(sys, fns[idx], (void *)&ctx, 0, &counter) == JOB_ID_INVALID) {
            fprintf(stderr, "dispatch failed at i=%d workers=%u\n", i, worker_count);
            break;
        }
    }

    /* Wait until all are done */
    (void)job_system_wait_idle(sys);

    clock_gettime(CLOCK_MONOTONIC, &wall_end);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_end);

    double wall_ms = timespec_ms(&wall_end) - timespec_ms(&wall_start);
    double cpu_ms = timespec_ms(&cpu_end) - timespec_ms(&cpu_start);
    double idle_ms = wall_ms - cpu_ms;
    if (idle_ms < 0.0) idle_ms = 0.0;
    double util = (wall_ms > 0.0) ? (cpu_ms / wall_ms) : 0.0;
    double throughput = (wall_ms > 0.0) ? (1000.0 / (wall_ms / 1000.0)) : 0.0;

    uint64_t started = job_system_jobs_started(sys);
    uint64_t completed = job_system_jobs_completed(sys);

    printf("workers=%u wall_ms=%.2f cpu_ms=%.2f idle_ms=%.2f util=%.3f jobs_started=%llu jobs_completed=%llu throughput=%.1f jobs/s\n",
           worker_count, wall_ms, cpu_ms, idle_ms, util,
           (unsigned long long)started, (unsigned long long)completed, throughput);

    if (job_system_queue_diag_supported()) {
        job_queue_diag_snapshot_t qd;
        job_system_queue_diag_snapshot(sys, &qd);

        double enq_scan_per = (qd.enqueue_calls > 0) ? ((double)qd.enqueue_scanned_slots / (double)qd.enqueue_calls) : 0.0;
        double pop_scan_per = (qd.pop_calls > 0) ? ((double)qd.pop_scanned_slots / (double)qd.pop_calls) : 0.0;

        printf("  qdiag enq_calls=%llu enq_succ=%llu enq_scan/call=%.2f enq_claim_fail=%llu pop_calls=%llu pop_succ=%llu pop_scan/call=%.2f pop_ready_seen=%llu pop_claim_fail=%llu cond_waits=%llu\n",
               (unsigned long long)qd.enqueue_calls,
               (unsigned long long)qd.enqueue_success,
               enq_scan_per,
               (unsigned long long)qd.enqueue_claim_fail,
               (unsigned long long)qd.pop_calls,
               (unsigned long long)qd.pop_success,
               pop_scan_per,
               (unsigned long long)qd.pop_ready_seen,
               (unsigned long long)qd.pop_claim_fail,
               (unsigned long long)qd.cond_waits);
    }

    job_system_shutdown(sys);
    /* Reduce per-worker sinks to a single value to prevent optimization. */
    volatile float sink_total = 0.0f;
    for (uint32_t i = 0; i < ctx.sink_count; ++i) {
        sink_total += ctx.sinks[i];
    }
    (void)sink_total;
}

int main(void) {
    /* Ensure instrumentation is explicitly disabled for clean perf output */
    job_instrument_enable(0);
    uint32_t counts[] = {1, 2, 4, 8, 16, 32, 64};
    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    if (nproc < 1) nproc = 1;
    for (size_t i = 0; i < ARRAY_SIZE(counts); ++i) {
        uint32_t capped = counts[i];
        if ((long)capped > nproc) capped = (uint32_t)nproc;
        run_once(capped);
    }
    return 0;
}
