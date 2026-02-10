/**
 * @file p007_net_prediction_tests.c
 * @brief RED tests for client-side prediction and reconciliation.
 *
 * Covers:
 *   1. Input ring buffer stores and retrieves inputs by tick
 *   2. Prediction applies unconfirmed inputs to produce predicted state
 *   3. No-error reconciliation: prediction matches server → no correction
 *   4. Small error: blends toward server state (soft correction)
 *   5. Large error: hard snaps to server state
 *   6. Reconciliation replays unconfirmed inputs after rewind
 *   7. Ring buffer wraps and discards old inputs
 *   8. NULL safety
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ferrum/net/prediction.h"

/* ── Minimal test harness ───────────────────────────────────────── */

static int g_pass_count;
static int g_fail_count;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-52s ", #name); \
    name(); \
    printf("PASS\n"); \
    g_pass_count++; \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail_count++; return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))

/* ── Helpers ────────────────────────────────────────────────────── */

static float fabsf_local(float x) { return x < 0.0f ? -x : x; }

/**
 * Trivial simulation step: position += input * dt.
 * Used as the resim callback.
 */
static void simple_sim_step(net_predict_state_t *state,
                            const net_predict_input_t *input,
                            void *user) {
    (void)user;
    float dt = 1.0f / 60.0f;
    state->position[0] += input->move[0] * dt;
    state->position[1] += input->move[1] * dt;
    state->position[2] += input->move[2] * dt;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Input ring buffer: store inputs and retrieve them by tick.
 */
TEST(test_input_ring_store_retrieve) {
    net_predict_input_t ring[8];
    net_predict_input_ring_t ir;
    net_predict_input_ring_init(&ir, ring, 8);

    /* Store 3 inputs at ticks 10, 11, 12. */
    net_predict_input_t in0 = { .tick = 10, .move = {1.0f, 0.0f, 0.0f} };
    net_predict_input_t in1 = { .tick = 11, .move = {0.0f, 1.0f, 0.0f} };
    net_predict_input_t in2 = { .tick = 12, .move = {0.0f, 0.0f, 1.0f} };

    ASSERT_EQ(net_predict_input_ring_push(&ir, &in0), NET_PREDICT_OK);
    ASSERT_EQ(net_predict_input_ring_push(&ir, &in1), NET_PREDICT_OK);
    ASSERT_EQ(net_predict_input_ring_push(&ir, &in2), NET_PREDICT_OK);

    /* Retrieve by tick. */
    const net_predict_input_t *found;
    found = net_predict_input_ring_get(&ir, 10);
    ASSERT(found != NULL);
    ASSERT(found->move[0] == 1.0f);

    found = net_predict_input_ring_get(&ir, 11);
    ASSERT(found != NULL);
    ASSERT(found->move[1] == 1.0f);

    found = net_predict_input_ring_get(&ir, 12);
    ASSERT(found != NULL);
    ASSERT(found->move[2] == 1.0f);

    /* Non-existent tick returns NULL. */
    found = net_predict_input_ring_get(&ir, 99);
    ASSERT(found == NULL);
}

/**
 * Prediction: applying unconfirmed inputs advances state.
 */
TEST(test_prediction_advances_state) {
    net_predict_input_t ring[16];
    net_predict_input_ring_t ir;
    net_predict_input_ring_init(&ir, ring, 16);

    /* Push 3 inputs: move +1 on X each tick. */
    for (uint32_t t = 0; t < 3; t++) {
        net_predict_input_t in = {
            .tick = t, .move = {60.0f, 0.0f, 0.0f}
        };
        net_predict_input_ring_push(&ir, &in);
    }

    /* Start at origin. */
    net_predict_state_t state;
    memset(&state, 0, sizeof(state));

    /* Simulate ticks 0..2 using the ring. */
    for (uint32_t t = 0; t < 3; t++) {
        const net_predict_input_t *in = net_predict_input_ring_get(&ir, t);
        ASSERT(in != NULL);
        simple_sim_step(&state, in, NULL);
    }

    /* 3 steps × (60 * 1/60) = 3.0 on X. */
    ASSERT(fabsf_local(state.position[0] - 3.0f) < 0.01f);
}

/**
 * No-error reconciliation: when prediction matches server state
 * exactly, no correction is applied.
 */
TEST(test_reconcile_no_error) {
    net_predict_ctx_t ctx;
    net_predict_input_t ring[16];
    net_predict_config_t cfg = {
        .snap_threshold = 5.0f,
        .blend_threshold = 0.1f,
        .blend_rate = 0.2f,
    };
    net_predict_init(&ctx, ring, 16, &cfg, simple_sim_step, NULL);

    /* Push input at tick 0 that moves +60 on X. */
    net_predict_input_t in0 = { .tick = 0, .move = {60.0f, 0.0f, 0.0f} };
    net_predict_input_ring_push(&ctx.input_ring, &in0);

    /* Client predicted state: applied tick 0 → x=1.0. */
    ctx.predicted.position[0] = 1.0f;
    ctx.predicted_tick = 1;

    /* Server confirms tick 0 with exactly x=1.0. */
    net_predict_state_t server_state;
    memset(&server_state, 0, sizeof(server_state));
    server_state.position[0] = 1.0f;

    int rc = net_predict_reconcile(&ctx, &server_state, 0);
    ASSERT_EQ(rc, NET_PREDICT_OK);

    /* No correction needed — state unchanged. */
    ASSERT(fabsf_local(ctx.predicted.position[0] - 1.0f) < 0.01f);
}

/**
 * Small error: below snap_threshold, above blend_threshold.
 * State blends toward server.
 */
TEST(test_reconcile_small_error_blends) {
    net_predict_ctx_t ctx;
    net_predict_input_t ring[16];
    net_predict_config_t cfg = {
        .snap_threshold = 5.0f,
        .blend_threshold = 0.01f,
        .blend_rate = 0.5f,
    };
    net_predict_init(&ctx, ring, 16, &cfg, simple_sim_step, NULL);

    /* No unconfirmed inputs — just test the blend. */
    ctx.confirmed_tick = 0;
    ctx.predicted_tick = 0;
    ctx.predicted.position[0] = 1.0f;

    /* Server says x=1.2 at tick 0. Error=0.2. */
    net_predict_state_t server;
    memset(&server, 0, sizeof(server));
    server.position[0] = 1.2f;

    net_predict_reconcile(&ctx, &server, 0);

    /* Should have blended: pos = lerp(1.0, 1.2, 0.5) = 1.1.
     * But reconcile replays from server state, so with no unconfirmed
     * inputs the result is the server state blended toward predicted.
     * Actually: rewind to server(1.2), replay 0 inputs → 1.2,
     * then blend old predicted (1.0) toward replayed (1.2) by 0.5.
     * result = 1.0 + (1.2 - 1.0)*0.5 = 1.1. */
    ASSERT(fabsf_local(ctx.predicted.position[0] - 1.1f) < 0.01f);
}

/**
 * Large error: above snap_threshold → hard snap to server.
 */
TEST(test_reconcile_large_error_snaps) {
    net_predict_ctx_t ctx;
    net_predict_input_t ring[16];
    net_predict_config_t cfg = {
        .snap_threshold = 5.0f,
        .blend_threshold = 0.01f,
        .blend_rate = 0.2f,
    };
    net_predict_init(&ctx, ring, 16, &cfg, simple_sim_step, NULL);

    ctx.confirmed_tick = 0;
    ctx.predicted_tick = 0;
    ctx.predicted.position[0] = 0.0f;

    /* Server says x=100.0 at tick 0 — way off. */
    net_predict_state_t server;
    memset(&server, 0, sizeof(server));
    server.position[0] = 100.0f;

    net_predict_reconcile(&ctx, &server, 0);

    /* Should hard-snap to server state. */
    ASSERT(fabsf_local(ctx.predicted.position[0] - 100.0f) < 0.01f);
}

/**
 * Reconciliation replays unconfirmed inputs after rewinding to
 * the server-confirmed state.
 */
TEST(test_reconcile_replays_inputs) {
    net_predict_ctx_t ctx;
    net_predict_input_t ring[16];
    net_predict_config_t cfg = {
        .snap_threshold = 100.0f,
        .blend_threshold = 0.01f,
        .blend_rate = 0.5f,
    };
    net_predict_init(&ctx, ring, 16, &cfg, simple_sim_step, NULL);

    /* Push inputs for ticks 0, 1, 2: each moves +60 on X. */
    for (uint32_t t = 0; t < 3; t++) {
        net_predict_input_t in = {
            .tick = t, .move = {60.0f, 0.0f, 0.0f}
        };
        net_predict_input_ring_push(&ctx.input_ring, &in);
    }

    /* Client predicted through tick 2 → x=3.0. */
    ctx.predicted.position[0] = 3.0f;
    ctx.predicted_tick = 3;

    /* Server confirms tick 0 with x=0.5 (slight mismatch). */
    net_predict_state_t server;
    memset(&server, 0, sizeof(server));
    server.position[0] = 0.5f;

    net_predict_reconcile(&ctx, &server, 0);

    /* Rewind to server state (x=0.5), replay ticks 1,2 (+1.0 each).
     * Result: 0.5 + 1.0 + 1.0 = 2.5.
     * Error from old predicted (3.0): 0.5, below snap (100).
     * Blend: old(3.0) toward replayed(2.5) by 0.5 → 2.75. */
    ASSERT(fabsf_local(ctx.predicted.position[0] - 2.75f) < 0.05f);
}

/**
 * Ring wraps: oldest inputs are overwritten when capacity exceeded.
 */
TEST(test_ring_wraps) {
    net_predict_input_t ring[4]; /* small ring */
    net_predict_input_ring_t ir;
    net_predict_input_ring_init(&ir, ring, 4);

    /* Push 6 inputs → oldest 2 overwritten. */
    for (uint32_t t = 0; t < 6; t++) {
        net_predict_input_t in = {
            .tick = t, .move = {(float)t, 0.0f, 0.0f}
        };
        net_predict_input_ring_push(&ir, &in);
    }

    /* Ticks 0, 1 should be gone. */
    ASSERT(net_predict_input_ring_get(&ir, 0) == NULL);
    ASSERT(net_predict_input_ring_get(&ir, 1) == NULL);

    /* Ticks 2..5 should be present. */
    for (uint32_t t = 2; t < 6; t++) {
        const net_predict_input_t *found = net_predict_input_ring_get(&ir, t);
        ASSERT(found != NULL);
        ASSERT(found->move[0] == (float)t);
    }
}

/**
 * NULL safety for all public functions.
 */
TEST(test_null_safety) {
    net_predict_input_ring_init(NULL, NULL, 0); /* no-op */

    net_predict_input_t in = { .tick = 0 };
    ASSERT_EQ(net_predict_input_ring_push(NULL, &in), NET_PREDICT_ERR_INVALID);
    ASSERT(net_predict_input_ring_get(NULL, 0) == NULL);

    net_predict_init(NULL, NULL, 0, NULL, NULL, NULL); /* no-op */
    ASSERT_EQ(net_predict_reconcile(NULL, NULL, 0), NET_PREDICT_ERR_INVALID);
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    printf("p007_net_prediction_tests:\n");
    RUN(test_input_ring_store_retrieve);
    RUN(test_prediction_advances_state);
    RUN(test_reconcile_no_error);
    RUN(test_reconcile_small_error_blends);
    RUN(test_reconcile_large_error_snaps);
    RUN(test_reconcile_replays_inputs);
    RUN(test_ring_wraps);
    RUN(test_null_safety);
    printf("%d/%d tests passed\n", g_pass_count,
           g_pass_count + g_fail_count);
    return g_fail_count ? 1 : 0;
}
