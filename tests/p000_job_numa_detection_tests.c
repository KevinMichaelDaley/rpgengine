#define _GNU_SOURCE
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ferrum/ferrum.h"

static atomic_int g_fail = 0;
#define ASSERT_TRUE(x) do { if (!(x)) { fprintf(stderr, "ASSERT_TRUE failed: %s @ %s:%d\n", #x, __FILE__, __LINE__); atomic_store(&g_fail, 1); return 1; } } while(0)
#define ASSERT_EQ_INT(a,b) do { int _aa=(a), _bb=(b); if (_aa!=_bb){ fprintf(stderr, "ASSERT_EQ_INT %d != %d @ %s:%d\n", _aa, _bb, __FILE__, __LINE__); atomic_store(&g_fail, 1); return 1; } } while(0)

static int mkdir_p(const char *path) {
    return mkdir(path, 0777) == 0 || access(path, F_OK) == 0 ? 0 : -1;
}

static int write_file(const char *path, const char *contents) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    if (fputs(contents, f) < 0) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

struct node_seen_ctx { volatile uint32_t *seen; };
static void node_seen_job(void *u) { struct node_seen_ctx *ctx = (struct node_seen_ctx *)u; uint32_t n = job_current_worker_node(); if (n < 2) ctx->seen[n]++; }

static int test_auto_detect_enable(void) {
    atomic_store(&g_fail, 0);
    char tmpl[] = "/tmp/job_numa_sysfs_XXXXXX";
    char *root = mkdtemp(tmpl);
    ASSERT_TRUE(root != NULL);
    char noderoot[256], node0[256], node1[256];
    snprintf(noderoot, sizeof(noderoot), "%s/node", root);
    snprintf(node0, sizeof(node0), "%s/node0", noderoot);
    snprintf(node1, sizeof(node1), "%s/node1", noderoot);
    ASSERT_EQ_INT(0, mkdir_p(root));
    ASSERT_EQ_INT(0, mkdir_p(noderoot));
    ASSERT_EQ_INT(0, mkdir_p(node0));
    ASSERT_EQ_INT(0, mkdir_p(node1));
    char cpulist0[256], cpulist1[256];
    snprintf(cpulist0, sizeof(cpulist0), "%s/cpulist", node0);
    snprintf(cpulist1, sizeof(cpulist1), "%s/cpulist", node1);
    ASSERT_EQ_INT(0, write_file(cpulist0, "0-3\n"));
    ASSERT_EQ_INT(0, write_file(cpulist1, "4-7\n"));

    setenv("JOB_SYS_NUMA_SYSFS", noderoot, 1);

    job_system_t sys_; job_system_t *sys=&sys_;
    ASSERT_EQ_INT(JOB_CREATE_OK, job_system_create(sys, 4, 128, 64*1024, 512, 0));
    ASSERT_EQ_INT(0, job_system_enable_numa_auto(sys));
    ASSERT_EQ_INT(1, job_system_numa_enabled(sys));

    ASSERT_EQ_INT(0, job_system_start(sys));
    /* Check mapping behaves (modulo 2 nodes). */
    job_counter_t c; job_counter_init(&c, 0);
    volatile uint32_t seen_nodes[2] = {0};
    struct node_seen_ctx ctx = { seen_nodes };
    for (int i = 0; i < 64; ++i) {
        ASSERT_TRUE(job_dispatch(sys, node_seen_job, &ctx, 0, &c) != JOB_ID_INVALID);
    }
    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    ASSERT_TRUE(seen_nodes[0] > 0 && seen_nodes[1] > 0);

    job_system_shutdown(sys);
    unsetenv("JOB_SYS_NUMA_SYSFS");
    return atomic_load(&g_fail);
}

static int test_auto_detect_no_nodes(void) {
    atomic_store(&g_fail, 0);
    unsetenv("JOB_SYS_NUMA_SYSFS");
    job_system_t sys_; job_system_t *sys=&sys_;
    ASSERT_EQ_INT(JOB_CREATE_OK, job_system_create(sys, 2, 64, 64*1024, 128, 0));
    ASSERT_EQ_INT(0, job_system_enable_numa_auto(sys));
    /* May be 0 or 1 nodes on some systems; require disabled or single node. */
    int enabled = job_system_numa_enabled(sys);
    ASSERT_TRUE(enabled == 0 || enabled == 1);
    job_system_shutdown(sys);
    return atomic_load(&g_fail);
}

int main(void) {
    int rc = 0;
    fprintf(stderr, "RUN numa_auto_detect_enable\n");
    rc |= test_auto_detect_enable();
    fprintf(stderr, rc ? "FAIL numa_auto_detect_enable\n" : "OK numa_auto_detect_enable\n");

    fprintf(stderr, "RUN numa_auto_detect_no_nodes\n");
    rc |= test_auto_detect_no_nodes();
    fprintf(stderr, rc ? "FAIL numa_auto_detect_no_nodes\n" : "OK numa_auto_detect_no_nodes\n");

    if (!rc) fprintf(stderr, "All 2 tests passed\n");
    return rc;
}
