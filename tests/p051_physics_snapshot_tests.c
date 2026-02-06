/**
 * @file p051_physics_snapshot_tests.c
 * @brief Unit tests for Network Snapshot Encoding (phys-116).
 *
 * Tests cover: vec3 quantization roundtrip, quaternion quantization
 * roundtrip, full encode/decode cycle, encoded size, flag preservation,
 * empty world, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ferrum/physics/world.h"
#include "ferrum/physics/snapshot.h"

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

/** @brief Return a default world config suitable for testing. */
static phys_world_config_t test_config(void)
{
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    cfg.max_colliders = 16;
    cfg.manifold_cache_size = 16;
    cfg.frame_arena_size = 4096;
    return cfg;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: Quantize/dequantize a vec3, verify error < 0.002 (1mm at scale 1000).
 */
static int test_quantize_vec3_roundtrip(void)
{
    phys_vec3_t v = {1.234f, -5.678f, 10.5f};
    int16_t packed[3];
    float scale = 1000.0f;

    phys_quantize_vec3(v, packed, scale);
    phys_vec3_t out = phys_dequantize_vec3(packed, 1.0f / scale);

    ASSERT_FLOAT_NEAR(v.x, out.x, 0.002f);
    ASSERT_FLOAT_NEAR(v.y, out.y, 0.002f);
    ASSERT_FLOAT_NEAR(v.z, out.z, 0.002f);
    return 0;
}

/**
 * Test 2: Quantize/dequantize a quaternion, verify dot product > 0.999.
 */
static int test_quantize_quat_roundtrip(void)
{
    /* Normalized quaternion representing ~45° rotation around Y. */
    phys_quat_t q = {0.0f, 0.3826834f, 0.0f, 0.9238795f};
    int16_t packed[3];

    phys_quantize_quat(q, packed);
    phys_quat_t out = phys_dequantize_quat(packed);

    float dot = q.x * out.x + q.y * out.y + q.z * out.z + q.w * out.w;
    ASSERT_TRUE(fabsf(dot) > 0.999f);

    /* Also test a quaternion where a different component is largest. */
    phys_quat_t q2 = {0.7071068f, 0.0f, 0.7071068f, 0.0f};
    phys_quantize_quat(q2, packed);
    phys_quat_t out2 = phys_dequantize_quat(packed);
    dot = q2.x * out2.x + q2.y * out2.y + q2.z * out2.z + q2.w * out2.w;
    ASSERT_TRUE(fabsf(dot) > 0.999f);

    return 0;
}

/**
 * Test 3: Encode a world with 5 bodies, decode, verify positions/orientations
 *         match within tolerance.
 */
static int test_snapshot_encode_decode(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Create 5 bodies with distinct positions. */
    for (int i = 0; i < 5; i++) {
        uint32_t idx = phys_world_create_body(&world);
        ASSERT_TRUE(idx != UINT32_MAX);
        phys_body_t *body = phys_world_get_body(&world, idx);
        ASSERT_TRUE(body != NULL);
        body->position = (phys_vec3_t){(float)i * 1.5f, (float)i * -0.5f, (float)i * 0.1f};
        body->orientation = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};
        body->linear_vel = (phys_vec3_t){(float)i * 0.1f, 0.0f, 0.0f};
        body->angular_vel = (phys_vec3_t){0.0f, (float)i * 0.05f, 0.0f};
    }
    world.tick_count = 42;

    /* Encode. */
    uint8_t buffer[1024];
    size_t encoded = phys_snapshot_encode(&world, buffer, sizeof(buffer));
    ASSERT_TRUE(encoded > 0);

    /* Decode. */
    phys_snapshot_body_t snap_bodies[16];
    phys_snapshot_t snapshot;
    snapshot.bodies = snap_bodies;
    ASSERT_TRUE(phys_snapshot_decode(buffer, encoded, &snapshot) == 0);

    ASSERT_TRUE(snapshot.tick == 42);
    ASSERT_TRUE(snapshot.body_count == 5);

    /* Verify first and last body positions match within tolerance. */
    /* Body 0: position (0,0,0). */
    phys_vec3_t p0 = phys_dequantize_vec3(snapshot.bodies[0].position, 1.0f / 1000.0f);
    ASSERT_FLOAT_NEAR(0.0f, p0.x, 0.002f);
    ASSERT_FLOAT_NEAR(0.0f, p0.y, 0.002f);
    ASSERT_FLOAT_NEAR(0.0f, p0.z, 0.002f);

    /* Body 4: position (6.0, -2.0, 0.4). */
    phys_vec3_t p4 = phys_dequantize_vec3(snapshot.bodies[4].position, 1.0f / 1000.0f);
    ASSERT_FLOAT_NEAR(6.0f, p4.x, 0.002f);
    ASSERT_FLOAT_NEAR(-2.0f, p4.y, 0.002f);
    ASSERT_FLOAT_NEAR(0.4f, p4.z, 0.002f);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 4: 10 bodies → encoded size = 12 + 10*26 = 272 bytes.
 */
static int test_snapshot_size(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    for (int i = 0; i < 10; i++) {
        uint32_t idx = phys_world_create_body(&world);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    uint8_t buffer[1024];
    size_t encoded = phys_snapshot_encode(&world, buffer, sizeof(buffer));
    ASSERT_TRUE(encoded == 12 + 10 * 26);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 5: Sleeping body flag preserved through encode/decode.
 */
static int test_snapshot_preserves_flags(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    uint32_t idx = phys_world_create_body(&world);
    ASSERT_TRUE(idx != UINT32_MAX);
    phys_body_t *body = phys_world_get_body(&world, idx);
    ASSERT_TRUE(body != NULL);
    body->flags = PHYS_BODY_FLAG_SLEEPING | PHYS_BODY_FLAG_KINEMATIC;
    body->tier = 3;

    uint8_t buffer[256];
    size_t encoded = phys_snapshot_encode(&world, buffer, sizeof(buffer));
    ASSERT_TRUE(encoded > 0);

    phys_snapshot_body_t snap_bodies[4];
    phys_snapshot_t snapshot;
    snapshot.bodies = snap_bodies;
    ASSERT_TRUE(phys_snapshot_decode(buffer, encoded, &snapshot) == 0);

    ASSERT_TRUE(snapshot.body_count == 1);
    /* Flags: SLEEPING (bit 2) | KINEMATIC (bit 1) = 0x06. */
    ASSERT_TRUE(snap_bodies[0].flags ==
                (PHYS_BODY_FLAG_SLEEPING | PHYS_BODY_FLAG_KINEMATIC));
    ASSERT_TRUE(snap_bodies[0].tier == 3);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 6: 0 bodies → small header only (12 bytes).
 */
static int test_snapshot_empty_world(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    uint8_t buffer[256];
    size_t encoded = phys_snapshot_encode(&world, buffer, sizeof(buffer));
    ASSERT_TRUE(encoded == 12);

    phys_snapshot_body_t snap_bodies[1];
    phys_snapshot_t snapshot;
    snapshot.bodies = snap_bodies;
    ASSERT_TRUE(phys_snapshot_decode(buffer, encoded, &snapshot) == 0);
    ASSERT_TRUE(snapshot.tick == 0);
    ASSERT_TRUE(snapshot.body_count == 0);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 7: NULL world/buffer doesn't crash, returns 0 or error.
 */
static int test_snapshot_null_safe(void)
{
    /* NULL world → encode returns 0. */
    uint8_t buffer[256];
    size_t encoded = phys_snapshot_encode(NULL, buffer, sizeof(buffer));
    ASSERT_TRUE(encoded == 0);

    /* NULL buffer → encode returns 0. */
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);
    encoded = phys_snapshot_encode(&world, NULL, 256);
    ASSERT_TRUE(encoded == 0);

    /* NULL buffer for decode → returns error. */
    ASSERT_TRUE(phys_snapshot_decode(NULL, 100, NULL) != 0);

    /* NULL snapshot_out for decode → returns error. */
    uint8_t dummy[64] = {0};
    ASSERT_TRUE(phys_snapshot_decode(dummy, sizeof(dummy), NULL) != 0);

    /* NULL world for apply → returns error. */
    phys_snapshot_t snapshot = {0};
    ASSERT_TRUE(phys_snapshot_apply(NULL, &snapshot) != 0);

    /* NULL snapshot for apply → returns error. */
    ASSERT_TRUE(phys_snapshot_apply(&world, NULL) != 0);

    phys_world_destroy(&world);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p051_physics_snapshot_tests:\n");

    RUN_TEST(test_quantize_vec3_roundtrip);
    RUN_TEST(test_quantize_quat_roundtrip);
    RUN_TEST(test_snapshot_encode_decode);
    RUN_TEST(test_snapshot_size);
    RUN_TEST(test_snapshot_preserves_flags);
    RUN_TEST(test_snapshot_empty_world);
    RUN_TEST(test_snapshot_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
