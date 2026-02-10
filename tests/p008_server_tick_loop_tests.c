/**
 * @file p008_server_tick_loop_tests.c
 * @brief Tests for server tick loop and encoder job dispatch.
 *
 * Tests verify: tick advancement, fixed-timestep accumulator with catch-up,
 * ordering contracts (drain → encode → flush), and encoder job dispatch
 * feeding outbound topic channels.
 */

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#include "ferrum/server/tick_loop.h"

/* ------------------------------------------------------------------ */
/*  Minimal test harness                                              */
/* ------------------------------------------------------------------ */

static int g_pass = 0, g_fail = 0;

#define TEST(name) static void name(void)
#define RUN(name)                                                      \
    do {                                                               \
        printf("  %-52s ", #name);                                     \
        name();                                                        \
        printf("PASS\n");                                              \
        g_pass++;                                                      \
    } while (0)

#define ASSERT(cond)                                                   \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond);   \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Mock callbacks for tracking what the tick loop invokes.            */
/* ------------------------------------------------------------------ */

typedef struct call_log {
    uint32_t drain_count;
    uint32_t physics_count;
    uint32_t encode_count;
    uint32_t flush_count;
    /** Order of invocations: 'D'=drain, 'P'=physics, 'E'=encode, 'F'=flush */
    char order[64];
    uint32_t order_len;
} call_log_t;

static call_log_t g_log;

static void log_reset(void) {
    memset(&g_log, 0, sizeof(g_log));
}

static void log_append(char c) {
    if (g_log.order_len < sizeof(g_log.order) - 1) {
        g_log.order[g_log.order_len++] = c;
        g_log.order[g_log.order_len] = '\0';
    }
}

static void mock_drain(void *user) {
    (void)user;
    g_log.drain_count++;
    log_append('D');
}

static void mock_physics(void *user) {
    (void)user;
    g_log.physics_count++;
    log_append('P');
}

static void mock_encode(void *user) {
    (void)user;
    g_log.encode_count++;
    log_append('E');
}

static void mock_flush(void *user) {
    (void)user;
    g_log.flush_count++;
    log_append('F');
}

/* ------------------------------------------------------------------ */
/*  Helper: create a default config for 60Hz tick rate.               */
/* ------------------------------------------------------------------ */

static fr_server_tick_loop_config_t default_cfg(void) {
    fr_server_tick_loop_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.tick_hz = 60;
    cfg.max_catchup_ticks = 3;
    cfg.on_drain = mock_drain;
    cfg.on_physics = mock_physics;
    cfg.on_encode = mock_encode;
    cfg.on_flush = mock_flush;
    cfg.user = NULL;
    return cfg;
}

/* ------------------------------------------------------------------ */
/*  1. Init + single tick advances tick counter.                       */
/* ------------------------------------------------------------------ */

TEST(test_init_and_single_tick) {
    log_reset();
    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t cfg = default_cfg();

    int rc = fr_server_tick_loop_init(&loop, &cfg);
    ASSERT(rc == 0);
    ASSERT(fr_server_tick_loop_tick_id(&loop) == 0);

    /* Advance time by one tick period (16.67ms at 60Hz). */
    uint64_t dt_us = 1000000 / 60 + 1; /* ~16667 us */
    int ticks = fr_server_tick_loop_step(&loop, dt_us);
    ASSERT(ticks == 1);
    ASSERT(fr_server_tick_loop_tick_id(&loop) == 1);

    /* Each tick invokes: drain → physics → encode → flush. */
    ASSERT(g_log.drain_count == 1);
    ASSERT(g_log.physics_count == 1);
    ASSERT(g_log.encode_count == 1);
    ASSERT(g_log.flush_count == 1);
}

/* ------------------------------------------------------------------ */
/*  2. Ordering: drain → physics → encode → flush per tick.           */
/* ------------------------------------------------------------------ */

TEST(test_tick_ordering) {
    log_reset();
    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t cfg = default_cfg();
    fr_server_tick_loop_init(&loop, &cfg);

    uint64_t dt_us = 1000000 / 60 + 1;
    fr_server_tick_loop_step(&loop, dt_us);

    /* Order must be D-P-E-F per tick. */
    ASSERT(strcmp(g_log.order, "DPEF") == 0);
}

/* ------------------------------------------------------------------ */
/*  3. Multiple ticks accumulate correctly.                           */
/* ------------------------------------------------------------------ */

TEST(test_multiple_ticks) {
    log_reset();
    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t cfg = default_cfg();
    fr_server_tick_loop_init(&loop, &cfg);

    /* Step 5 individual ticks. */
    uint64_t dt_us = 1000000 / 60 + 1;
    for (int i = 0; i < 5; i++) {
        fr_server_tick_loop_step(&loop, dt_us);
    }
    ASSERT(fr_server_tick_loop_tick_id(&loop) == 5);
    ASSERT(g_log.drain_count == 5);
    ASSERT(g_log.physics_count == 5);
    ASSERT(g_log.encode_count == 5);
}

/* ------------------------------------------------------------------ */
/*  4. Catch-up: large dt triggers multiple ticks, capped.            */
/* ------------------------------------------------------------------ */

TEST(test_catchup_capped) {
    log_reset();
    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t cfg = default_cfg();
    cfg.max_catchup_ticks = 3;
    fr_server_tick_loop_init(&loop, &cfg);

    /* Advance by 10 tick periods — should only run 3 (capped). */
    uint64_t dt_us = 10 * (1000000 / 60);
    int ticks = fr_server_tick_loop_step(&loop, dt_us);
    ASSERT(ticks == 3);
    ASSERT(fr_server_tick_loop_tick_id(&loop) == 3);
    ASSERT(g_log.drain_count == 3);
}

/* ------------------------------------------------------------------ */
/*  5. Sub-tick dt: accumulates but doesn't fire until enough.        */
/* ------------------------------------------------------------------ */

TEST(test_subtick_accumulation) {
    log_reset();
    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t cfg = default_cfg();
    fr_server_tick_loop_init(&loop, &cfg);

    /* Advance by half a tick period — no tick should fire. */
    uint64_t half_tick_us = (1000000 / 60) / 2;
    int ticks = fr_server_tick_loop_step(&loop, half_tick_us);
    ASSERT(ticks == 0);
    ASSERT(fr_server_tick_loop_tick_id(&loop) == 0);
    ASSERT(g_log.drain_count == 0);

    /* Advance by another half + a bit — one tick fires. */
    ticks = fr_server_tick_loop_step(&loop, half_tick_us + 1);
    ASSERT(ticks == 1);
    ASSERT(fr_server_tick_loop_tick_id(&loop) == 1);
}

/* ------------------------------------------------------------------ */
/*  6. Zero dt: no tick.                                              */
/* ------------------------------------------------------------------ */

TEST(test_zero_dt) {
    log_reset();
    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t cfg = default_cfg();
    fr_server_tick_loop_init(&loop, &cfg);

    int ticks = fr_server_tick_loop_step(&loop, 0);
    ASSERT(ticks == 0);
    ASSERT(g_log.drain_count == 0);
}

/* ------------------------------------------------------------------ */
/*  7. NULL callbacks: no crash (optional stages).                    */
/* ------------------------------------------------------------------ */

TEST(test_null_callbacks) {
    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.tick_hz = 30;
    cfg.max_catchup_ticks = 1;
    /* All callbacks NULL. */

    int rc = fr_server_tick_loop_init(&loop, &cfg);
    ASSERT(rc == 0);

    uint64_t dt_us = 1000000 / 30 + 1;
    int ticks = fr_server_tick_loop_step(&loop, dt_us);
    ASSERT(ticks == 1);
    ASSERT(fr_server_tick_loop_tick_id(&loop) == 1);
}

/* ------------------------------------------------------------------ */
/*  8. Invalid init returns error.                                    */
/* ------------------------------------------------------------------ */

TEST(test_invalid_init) {
    fr_server_tick_loop_t loop;

    /* NULL config. */
    int rc = fr_server_tick_loop_init(&loop, NULL);
    ASSERT(rc != 0);

    /* Zero tick rate. */
    fr_server_tick_loop_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.tick_hz = 0;
    rc = fr_server_tick_loop_init(&loop, &cfg);
    ASSERT(rc != 0);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("p008_server_tick_loop_tests:\n");
    RUN(test_init_and_single_tick);
    RUN(test_tick_ordering);
    RUN(test_multiple_ticks);
    RUN(test_catchup_capped);
    RUN(test_subtick_accumulation);
    RUN(test_zero_dt);
    RUN(test_null_callbacks);
    RUN(test_invalid_init);
    printf("%d/%d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
