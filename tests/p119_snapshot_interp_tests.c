/**
 * @file p119_snapshot_interp_tests.c
 * @brief Tests for fr_snapshot_interp_t — world-level snapshot interpolation.
 *
 * Tests: create/destroy, push stale rejection, sample interpolation,
 *        body count capping, edge cases (NULL, zero bodies).
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"
#include "ferrum/net/replication/interp/snapshot_interp.h"
#include "ferrum/physics/snapshot.h"

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",              \
                    __FILE__, __LINE__, #cond);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                     \
    do {                                                                     \
        float _e = (float)(exp);                                             \
        float _a = (float)(act);                                             \
        if (fabsf(_e - _a) > (eps)) {                                        \
            fprintf(stderr,                                                  \
                    "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f got %f "   \
                    "(eps=%f)\n",                                             \
                    __FILE__, __LINE__, (double)_e, (double)_a,              \
                    (double)(eps));                                           \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                     \
        if ((expected) != (actual)) {                                         \
            fprintf(stderr, "ASSERT_EQ failed: %s:%d: expected %d got %d\n", \
                    __FILE__, __LINE__, (int)(expected), (int)(actual));      \
            return 1;                                                        \
        }                                                                    \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────── */

/**
 * Build a minimal quantized snapshot body at a known position.
 * Position is quantized at scale=1000 (mm), so (1.0, 2.0, 3.0)
 * becomes (1000, 2000, 3000).
 */
static phys_snapshot_body_t make_body(float px, float py, float pz,
                                      float vx, float vy, float vz)
{
    phys_snapshot_body_t b;
    memset(&b, 0, sizeof(b));
    b.position[0]   = (int16_t)(px * 1000.0f);
    b.position[1]   = (int16_t)(py * 1000.0f);
    b.position[2]   = (int16_t)(pz * 1000.0f);
    /* Identity quaternion in smallest-3: w is largest, indices = 0. */
    b.orientation[0] = 0;
    b.orientation[1] = 0;
    b.orientation[2] = 0;
    b.linear_vel[0]  = (int16_t)(vx * 1000.0f);
    b.linear_vel[1]  = (int16_t)(vy * 1000.0f);
    b.linear_vel[2]  = (int16_t)(vz * 1000.0f);
    b.angular_vel[0] = 0;
    b.angular_vel[1] = 0;
    b.angular_vel[2] = 0;
    b.flags = 0;
    b.tier  = 0;
    return b;
}

/* ── Test: create and destroy ───────────────────────────────── */

static int test_create_destroy(void)
{
    fr_snapshot_interp_config_t cfg = { .max_bodies = 64, .quat_epsilon = 1e-6f };
    fr_snapshot_interp_t *si = fr_snapshot_interp_create(&cfg);
    ASSERT_TRUE(si != NULL);
    ASSERT_EQ(64, (int)si->max_bodies);
    fr_snapshot_interp_destroy(si);
    return 0;
}

/* ── Test: create with NULL / zero bodies returns NULL ─────── */

static int test_create_null_config(void)
{
    ASSERT_TRUE(fr_snapshot_interp_create(NULL) == NULL);

    fr_snapshot_interp_config_t cfg = { .max_bodies = 0 };
    ASSERT_TRUE(fr_snapshot_interp_create(&cfg) == NULL);
    return 0;
}

/* ── Test: push a snapshot and sample back ────────────────── */

static int test_push_and_sample(void)
{
    fr_snapshot_interp_config_t cfg = { .max_bodies = 4, .quat_epsilon = 1e-6f };
    fr_snapshot_interp_t *si = fr_snapshot_interp_create(&cfg);
    ASSERT_TRUE(si != NULL);

    /* Build a snapshot with 2 bodies. */
    phys_snapshot_body_t bodies[2] = {
        make_body(1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f),
        make_body(4.0f, 5.0f, 6.0f, 0.0f, 0.0f, 0.0f),
    };
    phys_snapshot_t snap = { .tick = 1, .body_count = 2, .bodies = bodies };

    uint32_t updated = fr_snapshot_interp_push(si, &snap, 1.0);
    ASSERT_EQ(2, (int)updated);

    /* Sample body 0 — should get approximately (1,2,3). */
    vec3_t pos;
    quat_t rot;
    ASSERT_TRUE(fr_snapshot_interp_sample(si, 0, 1.0, &pos, &rot));
    ASSERT_FLOAT_NEAR(1.0f, pos.x, 0.01f);
    ASSERT_FLOAT_NEAR(2.0f, pos.y, 0.01f);
    ASSERT_FLOAT_NEAR(3.0f, pos.z, 0.01f);

    /* Sample body 1. */
    ASSERT_TRUE(fr_snapshot_interp_sample(si, 1, 1.0, &pos, &rot));
    ASSERT_FLOAT_NEAR(4.0f, pos.x, 0.01f);
    ASSERT_FLOAT_NEAR(5.0f, pos.y, 0.01f);
    ASSERT_FLOAT_NEAR(6.0f, pos.z, 0.01f);

    fr_snapshot_interp_destroy(si);
    return 0;
}

/* ── Test: stale snapshot rejected ────────────────────────── */

static int test_stale_snapshot_rejected(void)
{
    fr_snapshot_interp_config_t cfg = { .max_bodies = 4, .quat_epsilon = 1e-6f };
    fr_snapshot_interp_t *si = fr_snapshot_interp_create(&cfg);

    phys_snapshot_body_t bodies[1] = { make_body(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f) };
    phys_snapshot_t snap = { .tick = 10, .body_count = 1, .bodies = bodies };

    ASSERT_EQ(1, (int)fr_snapshot_interp_push(si, &snap, 1.0));

    /* Same tick: should be rejected. */
    ASSERT_EQ(0, (int)fr_snapshot_interp_push(si, &snap, 1.1));

    /* Older tick: should be rejected. */
    snap.tick = 5;
    ASSERT_EQ(0, (int)fr_snapshot_interp_push(si, &snap, 1.2));

    fr_snapshot_interp_destroy(si);
    return 0;
}

/* ── Test: body count capped to max_bodies ────────────────── */

static int test_body_count_capped(void)
{
    fr_snapshot_interp_config_t cfg = { .max_bodies = 2, .quat_epsilon = 1e-6f };
    fr_snapshot_interp_t *si = fr_snapshot_interp_create(&cfg);

    /* Snapshot has 4 bodies but we only have room for 2. */
    phys_snapshot_body_t bodies[4] = {
        make_body(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        make_body(2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        make_body(3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        make_body(4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    };
    phys_snapshot_t snap = { .tick = 1, .body_count = 4, .bodies = bodies };

    uint32_t updated = fr_snapshot_interp_push(si, &snap, 1.0);
    ASSERT_EQ(2, (int)updated);

    /* Body 0 and 1 should be valid. */
    vec3_t pos;
    quat_t rot;
    ASSERT_TRUE(fr_snapshot_interp_sample(si, 0, 1.0, &pos, &rot));
    ASSERT_TRUE(fr_snapshot_interp_sample(si, 1, 1.0, &pos, &rot));

    /* Body 2 is out of range → false. */
    ASSERT_FALSE(fr_snapshot_interp_sample(si, 2, 1.0, &pos, &rot));

    fr_snapshot_interp_destroy(si);
    return 0;
}

/* ── Test: interpolation between two snapshots ────────────── */

static int test_interpolation_between_snapshots(void)
{
    fr_snapshot_interp_config_t cfg = { .max_bodies = 4, .quat_epsilon = 1e-6f };
    fr_snapshot_interp_t *si = fr_snapshot_interp_create(&cfg);

    /* First snapshot: body at x=0. */
    phys_snapshot_body_t b1[1] = { make_body(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f) };
    phys_snapshot_t s1 = { .tick = 60, .body_count = 1, .bodies = b1 };
    fr_snapshot_interp_push(si, &s1, 1.0);

    /* Second snapshot: body at x=1 (1 second later at 60Hz). */
    phys_snapshot_body_t b2[1] = { make_body(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f) };
    phys_snapshot_t s2 = { .tick = 120, .body_count = 1, .bodies = b2 };
    fr_snapshot_interp_push(si, &s2, 2.0);

    /* Sample at midpoint: should be approximately x=0.5. */
    vec3_t pos;
    quat_t rot;
    ASSERT_TRUE(fr_snapshot_interp_sample(si, 0, 1.5, &pos, &rot));
    ASSERT_FLOAT_NEAR(0.5f, pos.x, 0.1f);

    fr_snapshot_interp_destroy(si);
    return 0;
}

/* ── Test: sample with no data returns false ──────────────── */

static int test_sample_no_data(void)
{
    fr_snapshot_interp_config_t cfg = { .max_bodies = 4, .quat_epsilon = 1e-6f };
    fr_snapshot_interp_t *si = fr_snapshot_interp_create(&cfg);

    vec3_t pos;
    quat_t rot;
    /* No snapshots pushed — sample should fail. */
    ASSERT_FALSE(fr_snapshot_interp_sample(si, 0, 1.0, &pos, &rot));

    fr_snapshot_interp_destroy(si);
    return 0;
}

/* ── Test: NULL args ──────────────────────────────────────── */

static int test_null_args(void)
{
    ASSERT_EQ(0, (int)fr_snapshot_interp_push(NULL, NULL, 0.0));

    ASSERT_FALSE(fr_snapshot_interp_sample(NULL, 0, 0.0, NULL, NULL));

    fr_snapshot_interp_destroy(NULL); /* Should not crash. */
    return 0;
}

/* ── Main ─────────────────────────────────────────────────── */

typedef int (*test_fn)(void);

typedef struct {
    const char *name;
    test_fn     fn;
} test_entry_t;

int main(void)
{
    test_entry_t tests[] = {
        { "create_destroy",                  test_create_destroy },
        { "create_null_config",              test_create_null_config },
        { "push_and_sample",                 test_push_and_sample },
        { "stale_snapshot_rejected",         test_stale_snapshot_rejected },
        { "body_count_capped",              test_body_count_capped },
        { "interpolation_between_snapshots", test_interpolation_between_snapshots },
        { "sample_no_data",                  test_sample_no_data },
        { "null_args",                       test_null_args },
    };

    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;
    int failed = 0;

    for (int i = 0; i < n; i++) {
        int rc = tests[i].fn();
        if (rc == 0) {
            printf("  PASS  %s\n", tests[i].name);
            passed++;
        } else {
            printf("  FAIL  %s\n", tests[i].name);
            failed++;
        }
    }

    printf("\n%d/%d tests passed.\n", passed, n);
    return failed > 0 ? 1 : 0;
}
