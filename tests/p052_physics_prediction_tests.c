/**
 * @file p052_physics_prediction_tests.c
 * @brief Unit tests for Client Prediction Reconciliation (phys-118).
 *
 * Tests cover: default config, no-error reconciliation, small-error
 * blending, large-error snapping, body divergence check, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ferrum/physics/world.h"
#include "ferrum/physics/snapshot.h"
#include "ferrum/physics/prediction.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act), _t = (tol);                              \
        if (fabsf(_e - _a) > _t) {                                             \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f (tol %.6f)\n",                      \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)_t);    \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-50s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

/** @brief Return a small world config suitable for testing. */
static phys_world_config_t test_config(void)
{
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    cfg.max_colliders = 16;
    cfg.manifold_cache_size = 16;
    cfg.frame_arena_size = 4096;
    return cfg;
}

/**
 * @brief Create a snapshot body with the given position and identity orientation.
 *
 * Uses phys_quantize_vec3/quat to pack values the same way the encoder does.
 */
static phys_snapshot_body_t make_snap_body(float x, float y, float z)
{
    phys_snapshot_body_t sb;
    memset(&sb, 0, sizeof(sb));

    phys_vec3_t pos = {x, y, z};
    phys_quantize_vec3(pos, sb.position, 1000.0f);

    phys_quat_t identity = {0.0f, 0.0f, 0.0f, 1.0f};
    phys_quantize_quat(identity, sb.orientation);

    return sb;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: Default config has reasonable values.
 */
static int test_prediction_config_default(void)
{
    phys_prediction_config_t cfg = phys_prediction_config_default();

    ASSERT_FLOAT_NEAR(0.5f, cfg.position_snap_threshold, 0.001f);
    ASSERT_FLOAT_NEAR(0.1f, cfg.position_blend_rate, 0.001f);
    ASSERT_FLOAT_NEAR(0.5f, cfg.rotation_snap_threshold, 0.001f);
    ASSERT_FLOAT_NEAR(0.1f, cfg.rotation_blend_rate, 0.001f);

    return 0;
}

/**
 * Test 2: Local state matches server → all bodies_correct, no snaps/blends.
 */
static int test_prediction_no_error(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Create 2 bodies at known positions. */
    uint32_t idx0 = phys_world_create_body(&world);
    uint32_t idx1 = phys_world_create_body(&world);
    ASSERT_TRUE(idx0 != UINT32_MAX);
    ASSERT_TRUE(idx1 != UINT32_MAX);

    phys_body_t *b0 = phys_world_get_body(&world, idx0);
    phys_body_t *b1 = phys_world_get_body(&world, idx1);
    b0->position = (phys_vec3_t){1.0f, 2.0f, 3.0f};
    b0->orientation = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    b1->position = (phys_vec3_t){4.0f, 5.0f, 6.0f};
    b1->orientation = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    /* Build snapshot matching local positions (quantization may add tiny error). */
    phys_snapshot_body_t snap_bodies[2];
    snap_bodies[0] = make_snap_body(1.0f, 2.0f, 3.0f);
    snap_bodies[1] = make_snap_body(4.0f, 5.0f, 6.0f);
    phys_snapshot_t snapshot = {.tick = 10, .body_count = 2, .bodies = snap_bodies};

    phys_prediction_config_t pred_cfg = phys_prediction_config_default();
    phys_prediction_result_t result = phys_prediction_reconcile(
        &world, &snapshot, &pred_cfg);

    /* All bodies should be correct (quantization error < epsilon). */
    ASSERT_TRUE(result.bodies_correct == 2);
    ASSERT_TRUE(result.bodies_snapped == 0);
    ASSERT_TRUE(result.bodies_blended == 0);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 3: Local body 2cm off from server → bodies_blended = 1,
 *         position moves toward server after reconcile.
 */
static int test_prediction_small_error_blends(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    uint32_t idx = phys_world_create_body(&world);
    ASSERT_TRUE(idx != UINT32_MAX);

    phys_body_t *body = phys_world_get_body(&world, idx);
    /* Local position: slightly off from server. */
    body->position = (phys_vec3_t){1.02f, 2.0f, 3.0f};
    body->orientation = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    /* Server says body is at (1.0, 2.0, 3.0). */
    phys_snapshot_body_t snap_bodies[1];
    snap_bodies[0] = make_snap_body(1.0f, 2.0f, 3.0f);
    phys_snapshot_t snapshot = {.tick = 10, .body_count = 1, .bodies = snap_bodies};

    phys_prediction_config_t pred_cfg = phys_prediction_config_default();
    phys_prediction_result_t result = phys_prediction_reconcile(
        &world, &snapshot, &pred_cfg);

    ASSERT_TRUE(result.bodies_blended == 1);
    ASSERT_TRUE(result.bodies_snapped == 0);

    /* Position should have moved toward server (x < 1.02 after blend). */
    body = phys_world_get_body(&world, idx);
    ASSERT_TRUE(body->position.x < 1.02f);
    /* But not snapped exactly to server (blend is partial). */
    ASSERT_TRUE(body->position.x > 1.0f);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 4: Local body 1m off from server → bodies_snapped = 1,
 *         position set to server state.
 */
static int test_prediction_large_error_snaps(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    uint32_t idx = phys_world_create_body(&world);
    ASSERT_TRUE(idx != UINT32_MAX);

    phys_body_t *body = phys_world_get_body(&world, idx);
    /* Local position: 1m off on X axis. */
    body->position = (phys_vec3_t){2.0f, 2.0f, 3.0f};
    body->orientation = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    /* Server says body is at (1.0, 2.0, 3.0). */
    phys_snapshot_body_t snap_bodies[1];
    snap_bodies[0] = make_snap_body(1.0f, 2.0f, 3.0f);
    phys_snapshot_t snapshot = {.tick = 10, .body_count = 1, .bodies = snap_bodies};

    phys_prediction_config_t pred_cfg = phys_prediction_config_default();
    phys_prediction_result_t result = phys_prediction_reconcile(
        &world, &snapshot, &pred_cfg);

    ASSERT_TRUE(result.bodies_snapped == 1);
    ASSERT_TRUE(result.bodies_blended == 0);
    ASSERT_TRUE(result.max_position_error >= 0.9f);

    /* Position should be snapped close to server (within quantization tolerance). */
    body = phys_world_get_body(&world, idx);
    ASSERT_FLOAT_NEAR(1.0f, body->position.x, 0.01f);
    ASSERT_FLOAT_NEAR(2.0f, body->position.y, 0.01f);
    ASSERT_FLOAT_NEAR(3.0f, body->position.z, 0.01f);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 5: phys_prediction_body_diverged returns correct results.
 */
static int test_prediction_body_diverged(void)
{
    phys_vec3_t local_pos = {1.0f, 2.0f, 3.0f};
    phys_vec3_t server_pos_close = {1.01f, 2.0f, 3.0f};
    phys_vec3_t server_pos_far = {2.0f, 2.0f, 3.0f};

    /* Close positions: not diverged at 0.5m threshold. */
    ASSERT_TRUE(!phys_prediction_body_diverged(&local_pos, &server_pos_close, 0.5f));

    /* Far positions: diverged at 0.5m threshold. */
    ASSERT_TRUE(phys_prediction_body_diverged(&local_pos, &server_pos_far, 0.5f));

    /* Exact match: not diverged. */
    ASSERT_TRUE(!phys_prediction_body_diverged(&local_pos, &local_pos, 0.5f));

    return 0;
}

/**
 * Test 6: NULL world/snapshot doesn't crash.
 */
static int test_prediction_null_safe(void)
{
    phys_prediction_config_t pred_cfg = phys_prediction_config_default();

    /* NULL world. */
    phys_snapshot_body_t snap_bodies[1];
    snap_bodies[0] = make_snap_body(0.0f, 0.0f, 0.0f);
    phys_snapshot_t snapshot = {.tick = 1, .body_count = 1, .bodies = snap_bodies};

    phys_prediction_result_t r1 = phys_prediction_reconcile(
        NULL, &snapshot, &pred_cfg);
    ASSERT_TRUE(r1.bodies_correct == 0);
    ASSERT_TRUE(r1.bodies_snapped == 0);
    ASSERT_TRUE(r1.bodies_blended == 0);

    /* NULL snapshot. */
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    phys_prediction_result_t r2 = phys_prediction_reconcile(
        &world, NULL, &pred_cfg);
    ASSERT_TRUE(r2.bodies_correct == 0);
    ASSERT_TRUE(r2.bodies_snapped == 0);
    ASSERT_TRUE(r2.bodies_blended == 0);

    /* NULL config. */
    phys_prediction_result_t r3 = phys_prediction_reconcile(
        &world, &snapshot, NULL);
    ASSERT_TRUE(r3.bodies_correct == 0);
    ASSERT_TRUE(r3.bodies_snapped == 0);
    ASSERT_TRUE(r3.bodies_blended == 0);

    /* NULL pointers for diverged check. */
    ASSERT_TRUE(!phys_prediction_body_diverged(NULL, NULL, 0.5f));
    phys_vec3_t v = {0.0f, 0.0f, 0.0f};
    ASSERT_TRUE(!phys_prediction_body_diverged(&v, NULL, 0.5f));
    ASSERT_TRUE(!phys_prediction_body_diverged(NULL, &v, 0.5f));

    phys_world_destroy(&world);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p052_physics_prediction_tests:\n");

    RUN_TEST(test_prediction_config_default);
    RUN_TEST(test_prediction_no_error);
    RUN_TEST(test_prediction_small_error_blends);
    RUN_TEST(test_prediction_large_error_snaps);
    RUN_TEST(test_prediction_body_diverged);
    RUN_TEST(test_prediction_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
