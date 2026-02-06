/**
 * @file p023_physics_manifold_tests.c
 * @brief Unit tests for contact manifold structures (phys-008).
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "ferrum/physics/manifold.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(exp), (int)(act));                                                              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                                                 \
    do {                                                                                                 \
        float _e = (float)(exp);                                                                         \
        float _a = (float)(act);                                                                         \
        if (fabsf(_e - _a) > (eps)) {                                                                    \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f got %f (eps=%f)\n", __FILE__,  \
                    __LINE__, (double)_e, (double)_a, (double)(eps));                                     \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_VEC3_NEAR(exp, act, eps)                                                                  \
    do {                                                                                                 \
        phys_vec3_t _ev = (exp);                                                                         \
        phys_vec3_t _av = (act);                                                                         \
        if (fabsf(_ev.x - _av.x) > (eps) || fabsf(_ev.y - _av.y) > (eps) ||                             \
            fabsf(_ev.z - _av.z) > (eps)) {                                                              \
            fprintf(stderr,                                                                              \
                    "ASSERT_VEC3_NEAR failed: %s:%d: expected (%f,%f,%f) got (%f,%f,%f) (eps=%f)\n",      \
                    __FILE__, __LINE__, (double)_ev.x, (double)_ev.y, (double)_ev.z, (double)_av.x,      \
                    (double)_av.y, (double)_av.z, (double)(eps));                                         \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

/* ── Helper: create a contact point ─────────────────────────────── */

static phys_contact_point_t make_point(float wx, float wy, float wz,
                                       float nx, float ny, float nz,
                                       float penetration, uint32_t fid) {
    phys_contact_point_t p;
    memset(&p, 0, sizeof(p));
    p.point_world = (phys_vec3_t){wx, wy, wz};
    p.local_a     = (phys_vec3_t){wx, wy, wz};
    p.local_b     = (phys_vec3_t){wx, wy, wz};
    p.normal      = (phys_vec3_t){nx, ny, nz};
    p.penetration = penetration;
    p.feature_id  = fid;
    return p;
}

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_contact_point_size(void) {
    /* phys_contact_point_t: 4 * vec3(12) + float(4) + uint32(4) = 56 bytes */
    ASSERT_INT_EQ(56, (int)sizeof(phys_contact_point_t));
    return 0;
}

static int test_manifold_init(void) {
    phys_manifold_t m;
    phys_manifold_init(&m, 10, 20);
    ASSERT_INT_EQ(10, (int)m.body_a);
    ASSERT_INT_EQ(20, (int)m.body_b);
    ASSERT_INT_EQ(0, (int)m.point_count);
    ASSERT_FLOAT_NEAR(0.0f, m.friction, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, m.restitution, 1e-6f);
    return 0;
}

static int test_manifold_add_one_point(void) {
    phys_manifold_t m;
    phys_manifold_init(&m, 1, 2);

    phys_contact_point_t p = make_point(1.0f, 2.0f, 3.0f,
                                        0.0f, 1.0f, 0.0f,
                                        0.05f, 100);
    phys_manifold_add_point(&m, &p);

    ASSERT_INT_EQ(1, (int)m.point_count);
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, 2.0f, 3.0f}), m.points[0].point_world, 1e-6f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, 1.0f, 0.0f}), m.points[0].normal, 1e-6f);
    ASSERT_FLOAT_NEAR(0.05f, m.points[0].penetration, 1e-6f);
    ASSERT_INT_EQ(100, (int)m.points[0].feature_id);
    return 0;
}

static int test_manifold_add_four_points(void) {
    phys_manifold_t m;
    phys_manifold_init(&m, 1, 2);

    for (int i = 0; i < 4; i++) {
        phys_contact_point_t p = make_point((float)i, 0.0f, 0.0f,
                                            0.0f, 1.0f, 0.0f,
                                            0.01f * (float)(i + 1), (uint32_t)i);
        phys_manifold_add_point(&m, &p);
    }

    ASSERT_INT_EQ(4, (int)m.point_count);
    return 0;
}

static int test_manifold_add_fifth_triggers_reduce(void) {
    phys_manifold_t m;
    phys_manifold_init(&m, 1, 2);

    /* Add 5 points — after the 5th, reduce_points should cap at 4. */
    for (int i = 0; i < 5; i++) {
        phys_contact_point_t p = make_point((float)i, 0.0f, (float)(i * 2),
                                            0.0f, 1.0f, 0.0f,
                                            0.01f * (float)(i + 1), (uint32_t)i);
        phys_manifold_add_point(&m, &p);
    }

    ASSERT_INT_EQ(4, (int)m.point_count);
    return 0;
}

static int test_manifold_reduce_keeps_deepest(void) {
    phys_manifold_t m;
    phys_manifold_init(&m, 1, 2);

    /* Point 2 has the deepest penetration (0.5). */
    phys_contact_point_t pts[5];
    pts[0] = make_point(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.1f, 0);
    pts[1] = make_point(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.05f, 1);
    pts[2] = make_point(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 2);  /* deepest */
    pts[3] = make_point(1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.02f, 3);
    pts[4] = make_point(0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.01f, 4);

    for (int i = 0; i < 5; i++) {
        phys_manifold_add_point(&m, &pts[i]);
    }

    ASSERT_INT_EQ(4, (int)m.point_count);

    /* The deepest point (feature_id=2, pen=0.5) must be kept. */
    bool found_deepest = false;
    for (int i = 0; i < (int)m.point_count; i++) {
        if (m.points[i].feature_id == 2) {
            found_deepest = true;
            ASSERT_FLOAT_NEAR(0.5f, m.points[i].penetration, 1e-6f);
        }
    }
    ASSERT_TRUE(found_deepest);
    return 0;
}

static int test_manifold_reduce_spread(void) {
    phys_manifold_t m;
    phys_manifold_init(&m, 1, 2);

    /* Create 5 points with known spread — corners + center.
     * After reduction to 4, we expect the 4 corners to be kept
     * (better spatial coverage than including the center). */
    phys_contact_point_t pts[5];
    pts[0] = make_point(-1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.3f, 10);  /* deepest */
    pts[1] = make_point( 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.1f, 11);
    pts[2] = make_point( 1.0f, 0.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.1f, 12);
    pts[3] = make_point(-1.0f, 0.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.1f, 13);
    pts[4] = make_point( 0.0f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.1f, 14);  /* center */

    for (int i = 0; i < 5; i++) {
        phys_manifold_add_point(&m, &pts[i]);
    }

    ASSERT_INT_EQ(4, (int)m.point_count);

    /* Verify the bounding area of the reduced set is reasonable.
     * Compute min/max of world x and z. */
    float min_x = m.points[0].point_world.x;
    float max_x = m.points[0].point_world.x;
    float min_z = m.points[0].point_world.z;
    float max_z = m.points[0].point_world.z;
    for (int i = 1; i < (int)m.point_count; i++) {
        float px = m.points[i].point_world.x;
        float pz = m.points[i].point_world.z;
        if (px < min_x) min_x = px;
        if (px > max_x) max_x = px;
        if (pz < min_z) min_z = pz;
        if (pz > max_z) max_z = pz;
    }
    float area = (max_x - min_x) * (max_z - min_z);
    /* The 4 corners span a 2×2 = 4.0 area. Must be close to that. */
    ASSERT_TRUE(area >= 3.5f);
    return 0;
}

static int test_manifold_clear(void) {
    phys_manifold_t m;
    phys_manifold_init(&m, 1, 2);

    phys_contact_point_t p = make_point(1.0f, 0.0f, 0.0f,
                                        0.0f, 1.0f, 0.0f,
                                        0.1f, 0);
    phys_manifold_add_point(&m, &p);
    ASSERT_INT_EQ(1, (int)m.point_count);

    phys_manifold_clear(&m);
    ASSERT_INT_EQ(0, (int)m.point_count);
    return 0;
}

static int test_combine_friction(void) {
    /* geometric mean: sqrt(0.25 * 1.0) = 0.5 */
    ASSERT_FLOAT_NEAR(0.5f, phys_combine_friction(0.25f, 1.0f), 0.001f);
    /* sqrt(0.5 * 0.5) = 0.5 */
    ASSERT_FLOAT_NEAR(0.5f, phys_combine_friction(0.5f, 0.5f), 0.001f);
    /* sqrt(0 * 1) = 0 */
    ASSERT_FLOAT_NEAR(0.0f, phys_combine_friction(0.0f, 1.0f), 0.001f);
    return 0;
}

static int test_combine_restitution(void) {
    /* min(0.8, 0.5) = 0.5 */
    ASSERT_FLOAT_NEAR(0.5f, phys_combine_restitution(0.8f, 0.5f), 0.001f);
    /* min(1.0, 0.0) = 0.0 */
    ASSERT_FLOAT_NEAR(0.0f, phys_combine_restitution(1.0f, 0.0f), 0.001f);
    /* min(0.3, 0.3) = 0.3 */
    ASSERT_FLOAT_NEAR(0.3f, phys_combine_restitution(0.3f, 0.3f), 0.001f);
    return 0;
}

static int test_feature_id_edge(void) {
    uint32_t id = phys_feature_id_edge(2, 3);
    /* (2 << 8) | 3 = 512 + 3 = 515 */
    ASSERT_INT_EQ(515, (int)id);
    return 0;
}

static int test_feature_id_face(void) {
    uint32_t id = phys_feature_id_face(1);
    /* 0x10000 | 1 = 65537 */
    ASSERT_INT_EQ(65537, (int)id);
    return 0;
}

static int test_feature_id_vertex(void) {
    uint32_t id = phys_feature_id_vertex(5);
    /* 0x20000 | 5 = 131077 */
    ASSERT_INT_EQ(131077, (int)id);
    return 0;
}

static int test_manifold_null_safe(void) {
    /* All functions must handle NULL without crash. */
    phys_manifold_init(NULL, 0, 0);
    phys_manifold_add_point(NULL, NULL);
    phys_manifold_reduce_points(NULL);
    phys_manifold_clear(NULL);

    /* add_point with NULL point should not crash. */
    phys_manifold_t m;
    phys_manifold_init(&m, 1, 2);
    phys_manifold_add_point(&m, NULL);
    ASSERT_INT_EQ(0, (int)m.point_count);
    return 0;
}

static int test_warmstart_impulses_zeroed(void) {
    phys_manifold_t m;
    phys_manifold_init(&m, 1, 2);

    for (int i = 0; i < PHYS_MAX_MANIFOLD_POINTS; i++) {
        ASSERT_FLOAT_NEAR(0.0f, m.normal_impulse[i], 1e-6f);
        ASSERT_FLOAT_NEAR(0.0f, m.tangent_impulse[i][0], 1e-6f);
        ASSERT_FLOAT_NEAR(0.0f, m.tangent_impulse[i][1], 1e-6f);
    }
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"contact_point_size",                test_contact_point_size},
    {"manifold_init",                     test_manifold_init},
    {"manifold_add_one_point",            test_manifold_add_one_point},
    {"manifold_add_four_points",          test_manifold_add_four_points},
    {"manifold_add_fifth_triggers_reduce", test_manifold_add_fifth_triggers_reduce},
    {"manifold_reduce_keeps_deepest",     test_manifold_reduce_keeps_deepest},
    {"manifold_reduce_spread",            test_manifold_reduce_spread},
    {"manifold_clear",                    test_manifold_clear},
    {"combine_friction",                  test_combine_friction},
    {"combine_restitution",              test_combine_restitution},
    {"feature_id_edge",                   test_feature_id_edge},
    {"feature_id_face",                   test_feature_id_face},
    {"feature_id_vertex",                 test_feature_id_vertex},
    {"manifold_null_safe",                test_manifold_null_safe},
    {"warmstart_impulses_zeroed",         test_warmstart_impulses_zeroed},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
