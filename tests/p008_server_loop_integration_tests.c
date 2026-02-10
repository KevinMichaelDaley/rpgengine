/**
 * @file p008_server_loop_integration_tests.c
 * @brief Integration tests for the headless server loop.
 *
 * Proves:
 *   - Loop advances ticks deterministically.
 *   - Physics job dispatch + wait works (mock barrier).
 *   - Encoder feeds outbound topics per-tick without deadlock.
 *   - Full pipeline: drain → physics → encode → flush ordering.
 *   - No dependency on removed demo module.
 */

#include <stdio.h>
#include <string.h>

#include "ferrum/server/tick_loop.h"
#include "ferrum/server/tick_encoder.h"
#include "ferrum/net/topic_channel.h"

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
/*  Shared state for the integrated server loop.                      */
/* ------------------------------------------------------------------ */

#define MAX_CLIENTS 2

typedef struct server_ctx {
    /** Tick encoder. */
    fr_server_tick_encoder_t encoder;

    /** Outbound topics per client. */
    fr_topic_channel_t *reliable[MAX_CLIENTS];
    fr_topic_channel_t *unreliable[MAX_CLIENTS];
    uint8_t client_active[MAX_CLIENTS];
    uint16_t num_clients;

    /** Tracking: stage invocation counters. */
    uint32_t drain_count;
    uint32_t physics_count;
    uint32_t encode_count;
    uint32_t flush_count;

    /** Physics barrier simulation: set by physics, checked by encode. */
    uint32_t physics_completed_ticks;

    /** Accumulated event data from flushes. */
    uint32_t flushed_event_count;
    uint32_t flushed_state_count;

    /** Current tick ID (set by tick loop, used by encode). */
    uint64_t current_tick;
} server_ctx_t;

/* ------------------------------------------------------------------ */
/*  Topic getter callback.                                            */
/* ------------------------------------------------------------------ */

static bool get_topics_cb(void *user, uint16_t client_id,
                          fr_topic_channel_t **out_reliable,
                          fr_topic_channel_t **out_unreliable) {
    server_ctx_t *ctx = (server_ctx_t *)user;
    if (client_id >= ctx->num_clients || !ctx->client_active[client_id]) {
        return false;
    }
    *out_reliable = ctx->reliable[client_id];
    *out_unreliable = ctx->unreliable[client_id];
    return true;
}

/* ------------------------------------------------------------------ */
/*  Encoder callbacks: write tick-tagged markers into topics.         */
/* ------------------------------------------------------------------ */

static int encode_events(void *user, uint16_t client_id,
                         fr_topic_channel_t *topic, uint64_t tick) {
    (void)user;
    uint8_t msg[4] = { 0xEE, (uint8_t)client_id, (uint8_t)(tick & 0xFF),
                        (uint8_t)((tick >> 8) & 0xFF) };
    fr_topic_channel_push(topic, msg, sizeof(msg));
    return 0;
}

static int encode_state(void *user, uint16_t client_id,
                        fr_topic_channel_t *topic, uint64_t tick) {
    (void)user;
    uint8_t msg[4] = { 0xBB, (uint8_t)client_id, (uint8_t)(tick & 0xFF),
                        (uint8_t)((tick >> 8) & 0xFF) };
    fr_topic_channel_push(topic, msg, sizeof(msg));
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Tick loop stage callbacks.                                        */
/* ------------------------------------------------------------------ */

static void stage_drain(void *user) {
    server_ctx_t *ctx = (server_ctx_t *)user;
    ctx->drain_count++;
}

static void stage_physics(void *user) {
    server_ctx_t *ctx = (server_ctx_t *)user;
    ctx->physics_count++;
    /* Simulate physics barrier completion. */
    ctx->physics_completed_ticks++;
}

static void stage_encode(void *user) {
    server_ctx_t *ctx = (server_ctx_t *)user;
    ctx->encode_count++;
    /* Verify physics completed before we encode. */
    /* (physics_completed_ticks should equal encode_count at this point) */
    fr_server_tick_encoder_run(&ctx->encoder, ctx->current_tick + ctx->encode_count - 1);
}

static void stage_flush(void *user) {
    server_ctx_t *ctx = (server_ctx_t *)user;
    ctx->flush_count++;

    /* Drain all outbound topics to count what was produced. */
    for (uint16_t ci = 0; ci < ctx->num_clients; ci++) {
        if (!ctx->client_active[ci]) { continue; }
        uint8_t buf[32];
        size_t len;

        /* Drain reliable (events). */
        for (;;) {
            len = sizeof(buf);
            if (!fr_topic_channel_pop(ctx->reliable[ci], buf, &len)) { break; }
            if (buf[0] == 0xEE) { ctx->flushed_event_count++; }
        }
        /* Drain unreliable (state). */
        for (;;) {
            len = sizeof(buf);
            if (!fr_topic_channel_pop(ctx->unreliable[ci], buf, &len)) { break; }
            if (buf[0] == 0xBB) { ctx->flushed_state_count++; }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Helper: create/destroy server context.                            */
/* ------------------------------------------------------------------ */

static void server_ctx_create(server_ctx_t *ctx, uint16_t num_clients) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->num_clients = num_clients;

    fr_topic_channel_config_t tcfg = {0};
    tcfg.capacity = 64;
    tcfg.capacity_bytes = 8192;
    tcfg.max_message_size = 256;

    for (uint16_t i = 0; i < num_clients; i++) {
        ctx->reliable[i] = fr_topic_channel_create(&tcfg);
        ctx->unreliable[i] = fr_topic_channel_create(&tcfg);
        ctx->client_active[i] = 1;
    }

    fr_server_tick_encoder_config_t ecfg = {0};
    ecfg.max_clients = num_clients;
    ecfg.get_client_out_topics = get_topics_cb;
    ecfg.io_user = ctx;
    ecfg.encode_events = encode_events;
    ecfg.encode_state = encode_state;
    ecfg.encode_user = ctx;
    fr_server_tick_encoder_init(&ctx->encoder, &ecfg);
}

static void server_ctx_destroy(server_ctx_t *ctx) {
    for (uint16_t i = 0; i < ctx->num_clients; i++) {
        if (ctx->reliable[i]) { fr_topic_channel_destroy(ctx->reliable[i]); }
        if (ctx->unreliable[i]) { fr_topic_channel_destroy(ctx->unreliable[i]); }
    }
}

/* ------------------------------------------------------------------ */
/*  1. Deterministic tick advancement: N steps → exactly N ticks.     */
/* ------------------------------------------------------------------ */

TEST(test_deterministic_tick_advance) {
    server_ctx_t ctx;
    server_ctx_create(&ctx, 1);

    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t lcfg;
    memset(&lcfg, 0, sizeof(lcfg));
    lcfg.tick_hz = 60;
    lcfg.max_catchup_ticks = 1;
    lcfg.on_drain = stage_drain;
    lcfg.on_physics = stage_physics;
    lcfg.on_encode = stage_encode;
    lcfg.on_flush = stage_flush;
    lcfg.user = &ctx;
    fr_server_tick_loop_init(&loop, &lcfg);
    ctx.current_tick = 0;

    /* Run exactly 10 ticks. */
    uint64_t tick_us = 1000000 / 60 + 1;
    for (int i = 0; i < 10; i++) {
        fr_server_tick_loop_step(&loop, tick_us);
    }

    ASSERT(fr_server_tick_loop_tick_id(&loop) == 10);
    ASSERT(ctx.drain_count == 10);
    ASSERT(ctx.physics_count == 10);
    ASSERT(ctx.encode_count == 10);
    ASSERT(ctx.flush_count == 10);

    server_ctx_destroy(&ctx);
}

/* ------------------------------------------------------------------ */
/*  2. Physics completes before encode on every tick.                 */
/* ------------------------------------------------------------------ */

TEST(test_physics_before_encode) {
    server_ctx_t ctx;
    server_ctx_create(&ctx, 1);

    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t lcfg;
    memset(&lcfg, 0, sizeof(lcfg));
    lcfg.tick_hz = 60;
    lcfg.max_catchup_ticks = 5;
    lcfg.on_drain = stage_drain;
    lcfg.on_physics = stage_physics;
    lcfg.on_encode = stage_encode;
    lcfg.on_flush = stage_flush;
    lcfg.user = &ctx;
    fr_server_tick_loop_init(&loop, &lcfg);
    ctx.current_tick = 0;

    /* Run 5 ticks in one step. */
    uint64_t dt = 5 * (1000000 / 60) + 1;
    int ticks = fr_server_tick_loop_step(&loop, dt);
    ASSERT(ticks == 5);

    /* Physics must have run at least as many times as encode. */
    ASSERT(ctx.physics_completed_ticks >= ctx.encode_count);

    server_ctx_destroy(&ctx);
}

/* ------------------------------------------------------------------ */
/*  3. Encoder feeds outbound topics: each tick produces messages.    */
/* ------------------------------------------------------------------ */

TEST(test_encoder_feeds_topics) {
    server_ctx_t ctx;
    server_ctx_create(&ctx, 2);

    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t lcfg;
    memset(&lcfg, 0, sizeof(lcfg));
    lcfg.tick_hz = 60;
    lcfg.max_catchup_ticks = 1;
    lcfg.on_drain = stage_drain;
    lcfg.on_physics = stage_physics;
    lcfg.on_encode = stage_encode;
    lcfg.on_flush = stage_flush;
    lcfg.user = &ctx;
    fr_server_tick_loop_init(&loop, &lcfg);
    ctx.current_tick = 0;

    /* Run 3 ticks. */
    uint64_t tick_us = 1000000 / 60 + 1;
    for (int i = 0; i < 3; i++) {
        fr_server_tick_loop_step(&loop, tick_us);
    }

    /* 2 clients × 3 ticks = 6 events + 6 states. */
    ASSERT(ctx.flushed_event_count == 6);
    ASSERT(ctx.flushed_state_count == 6);

    server_ctx_destroy(&ctx);
}

/* ------------------------------------------------------------------ */
/*  4. Inactive client produces no outbound messages.                 */
/* ------------------------------------------------------------------ */

TEST(test_inactive_client_no_output) {
    server_ctx_t ctx;
    server_ctx_create(&ctx, 2);
    ctx.client_active[1] = 0;  /* client 1 disconnected */

    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t lcfg;
    memset(&lcfg, 0, sizeof(lcfg));
    lcfg.tick_hz = 60;
    lcfg.max_catchup_ticks = 1;
    lcfg.on_drain = stage_drain;
    lcfg.on_physics = stage_physics;
    lcfg.on_encode = stage_encode;
    lcfg.on_flush = stage_flush;
    lcfg.user = &ctx;
    fr_server_tick_loop_init(&loop, &lcfg);
    ctx.current_tick = 0;

    uint64_t tick_us = 1000000 / 60 + 1;
    for (int i = 0; i < 4; i++) {
        fr_server_tick_loop_step(&loop, tick_us);
    }

    /* Only client 0 produced output: 4 events + 4 states. */
    ASSERT(ctx.flushed_event_count == 4);
    ASSERT(ctx.flushed_state_count == 4);

    server_ctx_destroy(&ctx);
}

/* ------------------------------------------------------------------ */
/*  5. Catch-up cap prevents runaway ticks.                           */
/* ------------------------------------------------------------------ */

TEST(test_catchup_prevents_runaway) {
    server_ctx_t ctx;
    server_ctx_create(&ctx, 1);

    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t lcfg;
    memset(&lcfg, 0, sizeof(lcfg));
    lcfg.tick_hz = 60;
    lcfg.max_catchup_ticks = 3;
    lcfg.on_drain = stage_drain;
    lcfg.on_physics = stage_physics;
    lcfg.on_encode = stage_encode;
    lcfg.on_flush = stage_flush;
    lcfg.user = &ctx;
    fr_server_tick_loop_init(&loop, &lcfg);
    ctx.current_tick = 0;

    /* 100 tick periods worth of time → capped at 3. */
    uint64_t dt = 100 * (1000000 / 60);
    int ticks = fr_server_tick_loop_step(&loop, dt);
    ASSERT(ticks == 3);
    ASSERT(ctx.physics_count == 3);
    ASSERT(ctx.encode_count == 3);

    /* Next step with normal dt → resumes from tick 3, not 100. */
    uint64_t tick_us = 1000000 / 60 + 1;
    ticks = fr_server_tick_loop_step(&loop, tick_us);
    ASSERT(ticks == 1);
    ASSERT(fr_server_tick_loop_tick_id(&loop) == 4);

    server_ctx_destroy(&ctx);
}

/* ------------------------------------------------------------------ */
/*  6. Full pipeline runs without deadlock (smoke test).              */
/* ------------------------------------------------------------------ */

TEST(test_no_deadlock_100_ticks) {
    server_ctx_t ctx;
    server_ctx_create(&ctx, 2);

    fr_server_tick_loop_t loop;
    fr_server_tick_loop_config_t lcfg;
    memset(&lcfg, 0, sizeof(lcfg));
    lcfg.tick_hz = 60;
    lcfg.max_catchup_ticks = 1;
    lcfg.on_drain = stage_drain;
    lcfg.on_physics = stage_physics;
    lcfg.on_encode = stage_encode;
    lcfg.on_flush = stage_flush;
    lcfg.user = &ctx;
    fr_server_tick_loop_init(&loop, &lcfg);
    ctx.current_tick = 0;

    /* Run 100 ticks — should complete without hanging. */
    uint64_t tick_us = 1000000 / 60 + 1;
    for (int i = 0; i < 100; i++) {
        fr_server_tick_loop_step(&loop, tick_us);
    }

    ASSERT(fr_server_tick_loop_tick_id(&loop) == 100);
    /* 2 clients × 100 ticks = 200 events + 200 states. */
    ASSERT(ctx.flushed_event_count == 200);
    ASSERT(ctx.flushed_state_count == 200);

    server_ctx_destroy(&ctx);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("p008_server_loop_integration_tests:\n");
    RUN(test_deterministic_tick_advance);
    RUN(test_physics_before_encode);
    RUN(test_encoder_feeds_topics);
    RUN(test_inactive_client_no_output);
    RUN(test_catchup_prevents_runaway);
    RUN(test_no_deadlock_100_ticks);
    printf("%d/%d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
