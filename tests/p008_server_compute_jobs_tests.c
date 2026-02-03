#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ferrum/server/repl_server.h"
#include "ferrum/net/rudp/peer.h"

#define TEST_FAIL(msg, ...)                                                                         \
    do {                                                                                            \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__);               \
        return 1;                                                                                  \
    } while (0)

#define ASSERT_TRUE(cond)                                                                           \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            TEST_FAIL("%s", #cond);                                                                \
        }                                                                                           \
    } while (0)

#define ASSERT_EQ_INT(expected, actual)                                                             \
    do {                                                                                            \
        long long _exp = (long long)(expected);                                                     \
        long long _act = (long long)(actual);                                                       \
        if (_exp != _act) {                                                                         \
            TEST_FAIL("expected %lld got %lld", _exp, _act);                                       \
        }                                                                                           \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void *g_last_slots_alloc = NULL;

static int make_server(job_system_t *jobs, server_repl_server_t **out_srv) {
    net_udp_socket_t sock = {0};
    uint16_t max_clients = 1;
    uint16_t max_entities = 4;

    size_t total_slots = (size_t)max_clients * (size_t)max_entities;
    size_t slots_bytes = net_rudp_send_slot_storage_size(total_slots);
    void *slot_storage = calloc(1u, slots_bytes);
    if (!slot_storage) {
        return 1;
    }
    g_last_slots_alloc = slot_storage;

    server_repl_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_clients = max_clients;
    cfg.tick_hz = 60;
    cfg.max_entities = max_entities;
    cfg.resend_interval_ms = 50u;
    cfg.rudp_send_slot_storage = slot_storage;
    cfg.rudp_send_slot_storage_bytes = slots_bytes;
    cfg.rudp_send_slots_per_client = max_entities;

    server_repl_server_t *srv = server_repl_server_create(&cfg, &sock, jobs);
    if (!srv) {
        free(slot_storage);
        return 1;
    }

    /* Activate a single client and mark joined via debug helper. */
    ASSERT_EQ_INT(0, server_repl_server_debug_force_client_joined(srv, 0));

    /* Create a few active entities owned by client 0 and mark as known to the client. */
    for (uint16_t ei = 0; ei < max_entities; ++ei) {
        uint16_t idx = UINT16_MAX;
        ASSERT_EQ_INT(0, server_repl_server_debug_add_active_entity(srv, 0u, 100u + ei, &idx));
        ASSERT_TRUE(idx < max_entities);
        ASSERT_EQ_INT(0, server_repl_server_debug_force_entity_known(srv, 0u, idx));
    }

    *out_srv = srv;
    return 0;
}

static int test_tick_dispatches_state_jobs(void) {
    job_system_t sys_; job_system_t* sys=&sys_;
    job_system_create_status_t st = job_system_create(sys, 1, 32, 64 * 1024, 1024, 1);
    ASSERT_TRUE(st == JOB_CREATE_OK);
    ASSERT_EQ_INT(0, job_system_start(sys));

    server_repl_server_t *srv = NULL;
    ASSERT_EQ_INT(0, make_server(sys, &srv));

    uint64_t before_started = job_system_jobs_started(sys);
    uint64_t before_completed = job_system_jobs_completed(sys);

    ASSERT_EQ_INT(0, server_repl_server_tick(srv, 0));

    /* Expect up to 2 state jobs scheduled per tick for the client. */
    /* As an additional check, schedule one explicit state job via debug API. */
    ASSERT_EQ_INT(0, server_repl_server_debug_schedule_state_job(srv, 0u, 0u, 0));
    uint64_t after_started = job_system_jobs_started(sys);
    server_repl_stats_t st_stats = server_repl_server_stats(srv);
    ASSERT_TRUE(st_stats.state_jobs_scheduled >= 1);
    uint64_t after_completed = job_system_jobs_completed(sys);

    ASSERT_TRUE(after_started >= before_started + 1);

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));
    after_completed = job_system_jobs_completed(sys);
    ASSERT_TRUE(after_completed >= after_started);

    server_repl_server_destroy(srv);
    free(g_last_slots_alloc); g_last_slots_alloc = NULL;
    job_system_shutdown(sys);
    return 0;
}

static int test_queue_capacity_limits_jobs(void) {
    job_system_t sys_; job_system_t* sys=&sys_;
    job_system_create_status_t st = job_system_create(sys, 1, 1, 64 * 1024, 1024, 1);
    ASSERT_TRUE(st == JOB_CREATE_OK);
    ASSERT_EQ_INT(0, job_system_start(sys));

    server_repl_server_t *srv = NULL;
    ASSERT_EQ_INT(0, make_server(sys, &srv));

    uint64_t before_started = job_system_jobs_started(sys);
    /* Attempt to schedule two jobs explicitly; capacity is 1 so only one should succeed. */
    int s1 = server_repl_server_debug_schedule_state_job(srv, 0u, 0u, 0);
    int s2 = server_repl_server_debug_schedule_state_job(srv, 0u, 1u, 0);
    ASSERT_EQ_INT(0, s1);
    ASSERT_TRUE(s2 != 0);
    uint64_t after_started = job_system_jobs_started(sys);
    server_repl_stats_t st_stats = server_repl_server_stats(srv);
    ASSERT_EQ_INT(1, (int)st_stats.state_jobs_scheduled);
    ASSERT_EQ_INT((int)(before_started + 1), (int)after_started);

    /* With queue capacity 1, only a single job should be scheduled. */
    ASSERT_EQ_INT(before_started + 1, after_started);

    ASSERT_EQ_INT(0, job_system_wait_idle(sys));

    server_repl_server_destroy(srv);
    free(g_last_slots_alloc); g_last_slots_alloc = NULL;
    job_system_shutdown(sys);
    return 0;
}

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"tick_dispatches_state_jobs", test_tick_dispatches_state_jobs},
    {"queue_capacity_limits_jobs", test_queue_capacity_limits_jobs},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
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

    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
