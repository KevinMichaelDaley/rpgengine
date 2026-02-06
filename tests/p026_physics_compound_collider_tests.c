/**
 * @file p026_physics_compound_collider_tests.c
 * @brief Unit tests for compound collider (animated hierarchy) (phys-003b).
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "ferrum/math/constants.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"
#include "ferrum/physics/compound_collider.h"

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

/* ── Helper ─────────────────────────────────────────────────────── */

static const phys_quat_t IDENTITY_QUAT = {0.0f, 0.0f, 0.0f, 1.0f};
static const phys_vec3_t ZERO_VEC3     = {0.0f, 0.0f, 0.0f};

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_compound_init(void) {
    phys_compound_collider_t cc;
    phys_compound_child_t storage[8];

    phys_compound_init(&cc, storage, 8);

    ASSERT_INT_EQ(0, cc.child_count);
    ASSERT_INT_EQ(8, cc.max_children);
    ASSERT_TRUE(cc.children == storage);

    /* Cached AABB should be zeroed. */
    ASSERT_FLOAT_NEAR(0.0f, cc.cached_aabb.min.x, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, cc.cached_aabb.max.x, 1e-6f);

    return 0;
}

static int test_compound_add_sphere_child(void) {
    phys_compound_collider_t cc;
    phys_compound_child_t storage[4];
    phys_compound_init(&cc, storage, 4);

    phys_collider_t col;
    phys_collider_init_sphere(&col, 0, (phys_vec3_t){1.0f, 2.0f, 3.0f});

    phys_sphere_t sphere = {.radius = 0.5f};
    phys_compound_add_child(&cc, &col, &sphere, 2);

    ASSERT_INT_EQ(1, cc.child_count);
    ASSERT_INT_EQ(PHYS_SHAPE_SPHERE, cc.children[0].collider.type);
    ASSERT_INT_EQ(2, cc.children[0].bone_index);
    ASSERT_FLOAT_NEAR(0.5f, cc.children[0].shape.sphere.radius, 1e-6f);
    ASSERT_FLOAT_NEAR(1.0f, cc.children[0].collider.local_offset.x, 1e-6f);

    return 0;
}

static int test_compound_add_box_child(void) {
    phys_compound_collider_t cc;
    phys_compound_child_t storage[4];
    phys_compound_init(&cc, storage, 4);

    phys_collider_t col;
    phys_collider_init_box(&col, 0, ZERO_VEC3, IDENTITY_QUAT);

    phys_box_t box = {.half_extents = {1.0f, 2.0f, 3.0f}};
    phys_compound_add_child(&cc, &col, &box, 0);

    ASSERT_INT_EQ(1, cc.child_count);
    ASSERT_INT_EQ(PHYS_SHAPE_BOX, cc.children[0].collider.type);
    ASSERT_FLOAT_NEAR(1.0f, cc.children[0].shape.box.half_extents.x, 1e-6f);
    ASSERT_FLOAT_NEAR(2.0f, cc.children[0].shape.box.half_extents.y, 1e-6f);
    ASSERT_FLOAT_NEAR(3.0f, cc.children[0].shape.box.half_extents.z, 1e-6f);

    return 0;
}

static int test_compound_add_capsule_child(void) {
    phys_compound_collider_t cc;
    phys_compound_child_t storage[4];
    phys_compound_init(&cc, storage, 4);

    phys_collider_t col;
    phys_collider_init_capsule(&col, 0, ZERO_VEC3, IDENTITY_QUAT);

    phys_capsule_t cap = {.radius = 0.3f, .half_height = 1.0f};
    phys_compound_add_child(&cc, &col, &cap, 1);

    ASSERT_INT_EQ(1, cc.child_count);
    ASSERT_INT_EQ(PHYS_SHAPE_CAPSULE, cc.children[0].collider.type);
    ASSERT_FLOAT_NEAR(0.3f, cc.children[0].shape.capsule.radius, 1e-6f);
    ASSERT_FLOAT_NEAR(1.0f, cc.children[0].shape.capsule.half_height, 1e-6f);

    return 0;
}

static int test_compound_add_at_capacity(void) {
    phys_compound_collider_t cc;
    phys_compound_child_t storage[2];
    phys_compound_init(&cc, storage, 2);

    phys_collider_t col;
    phys_collider_init_sphere(&col, 0, ZERO_VEC3);
    phys_sphere_t sphere = {.radius = 1.0f};

    phys_compound_add_child(&cc, &col, &sphere, 0);
    phys_compound_add_child(&cc, &col, &sphere, 1);
    /* Third add should be silently ignored — already at max. */
    phys_compound_add_child(&cc, &col, &sphere, 2);

    ASSERT_INT_EQ(2, cc.child_count);

    return 0;
}

static int test_compound_bone_update(void) {
    phys_compound_collider_t cc;
    phys_compound_child_t storage[4];
    phys_compound_init(&cc, storage, 4);

    /* Add sphere at bone 0 and box at bone 1. */
    phys_collider_t col_s;
    phys_collider_init_sphere(&col_s, 0, ZERO_VEC3);
    phys_sphere_t sphere = {.radius = 0.5f};
    phys_compound_add_child(&cc, &col_s, &sphere, 0);

    phys_collider_t col_b;
    phys_collider_init_box(&col_b, 0, ZERO_VEC3, IDENTITY_QUAT);
    phys_box_t box = {.half_extents = {1.0f, 1.0f, 1.0f}};
    phys_compound_add_child(&cc, &col_b, &box, 1);

    /* Bone data. */
    phys_quat_t bone_rots[2] = {IDENTITY_QUAT, IDENTITY_QUAT};
    phys_vec3_t bone_poss[2] = {{5.0f, 0.0f, 0.0f}, {0.0f, 10.0f, 0.0f}};

    phys_compound_update_transforms(&cc, bone_rots, bone_poss, 2);

    /* Child 0 should now have offset (5,0,0). */
    ASSERT_VEC3_NEAR(((phys_vec3_t){5.0f, 0.0f, 0.0f}),
                     cc.children[0].collider.local_offset, 1e-6f);
    /* Child 1 should now have offset (0,10,0). */
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, 10.0f, 0.0f}),
                     cc.children[1].collider.local_offset, 1e-6f);

    return 0;
}

static int test_compound_static_bone_unchanged(void) {
    phys_compound_collider_t cc;
    phys_compound_child_t storage[4];
    phys_compound_init(&cc, storage, 4);

    /* Add sphere with bone_index = 0xFFFF (static). */
    phys_collider_t col;
    phys_collider_init_sphere(&col, 0, (phys_vec3_t){7.0f, 8.0f, 9.0f});
    phys_sphere_t sphere = {.radius = 1.0f};
    phys_compound_add_child(&cc, &col, &sphere, 0xFFFF);

    /* Update with some bone data — static child should be unaffected. */
    phys_quat_t bone_rots[1] = {IDENTITY_QUAT};
    phys_vec3_t bone_poss[1] = {{99.0f, 99.0f, 99.0f}};
    phys_compound_update_transforms(&cc, bone_rots, bone_poss, 1);

    ASSERT_VEC3_NEAR(((phys_vec3_t){7.0f, 8.0f, 9.0f}),
                     cc.children[0].collider.local_offset, 1e-6f);

    return 0;
}

static int test_compound_aabb_single_sphere(void) {
    phys_compound_collider_t cc;
    phys_compound_child_t storage[4];
    phys_compound_init(&cc, storage, 4);

    /* Sphere at local offset (2,0,0) with radius 1. */
    phys_collider_t col;
    phys_collider_init_sphere(&col, 0, (phys_vec3_t){2.0f, 0.0f, 0.0f});
    phys_sphere_t sphere = {.radius = 1.0f};
    phys_compound_add_child(&cc, &col, &sphere, 0xFFFF);

    /* Body at origin, identity rotation. */
    phys_aabb_t aabb;
    phys_compound_compute_aabb(&cc, ZERO_VEC3, IDENTITY_QUAT, &aabb);

    /* World center = (0,0,0) + rotate_identity((2,0,0)) = (2,0,0).
     * AABB = (2-1, 0-1, 0-1) to (2+1, 0+1, 0+1) = (1,-1,-1) to (3,1,1). */
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, -1.0f, -1.0f}), aabb.min, 1e-4f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){3.0f, 1.0f, 1.0f}), aabb.max, 1e-4f);

    return 0;
}

static int test_compound_aabb_two_children(void) {
    phys_compound_collider_t cc;
    phys_compound_child_t storage[4];
    phys_compound_init(&cc, storage, 4);

    /* Child 0: sphere at (-5, 0, 0), radius 1. */
    phys_collider_t col0;
    phys_collider_init_sphere(&col0, 0, (phys_vec3_t){-5.0f, 0.0f, 0.0f});
    phys_sphere_t s0 = {.radius = 1.0f};
    phys_compound_add_child(&cc, &col0, &s0, 0xFFFF);

    /* Child 1: sphere at (5, 0, 0), radius 1. */
    phys_collider_t col1;
    phys_collider_init_sphere(&col1, 0, (phys_vec3_t){5.0f, 0.0f, 0.0f});
    phys_sphere_t s1 = {.radius = 1.0f};
    phys_compound_add_child(&cc, &col1, &s1, 0xFFFF);

    phys_aabb_t aabb;
    phys_compound_compute_aabb(&cc, ZERO_VEC3, IDENTITY_QUAT, &aabb);

    /* Should encompass both: min=(-6,-1,-1), max=(6,1,1). */
    ASSERT_VEC3_NEAR(((phys_vec3_t){-6.0f, -1.0f, -1.0f}), aabb.min, 1e-4f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){6.0f, 1.0f, 1.0f}), aabb.max, 1e-4f);

    return 0;
}

static int test_compound_aabb_rotated_body(void) {
    phys_compound_collider_t cc;
    phys_compound_child_t storage[4];
    phys_compound_init(&cc, storage, 4);

    /* Single sphere child at local offset (2, 0, 0), radius 0.5. */
    phys_collider_t col;
    phys_collider_init_sphere(&col, 0, (phys_vec3_t){2.0f, 0.0f, 0.0f});
    phys_sphere_t sphere = {.radius = 0.5f};
    phys_compound_add_child(&cc, &col, &sphere, 0xFFFF);

    /* Body at origin, rotated 90° around Y:
     * local X -> world -Z. So child world center = (0, 0, -2). */
    phys_quat_t rot90y = quat_from_axis_angle((vec3_t){0.0f, 1.0f, 0.0f},
                                               FERRUM_PI / 2.0f, 1e-6f);
    phys_aabb_t aabb;
    phys_compound_compute_aabb(&cc, ZERO_VEC3, rot90y, &aabb);

    /* World center ≈ (0, 0, -2), AABB ≈ (-0.5, -0.5, -2.5) to (0.5, 0.5, -1.5). */
    ASSERT_FLOAT_NEAR(-0.5f, aabb.min.x, 1e-3f);
    ASSERT_FLOAT_NEAR(-0.5f, aabb.min.y, 1e-3f);
    ASSERT_FLOAT_NEAR(-2.5f, aabb.min.z, 1e-3f);
    ASSERT_FLOAT_NEAR(0.5f, aabb.max.x, 1e-3f);
    ASSERT_FLOAT_NEAR(0.5f, aabb.max.y, 1e-3f);
    ASSERT_FLOAT_NEAR(-1.5f, aabb.max.z, 1e-3f);

    return 0;
}

static int test_compound_null_safe(void) {
    /* All functions should handle NULL without crashing. */
    phys_compound_init(NULL, NULL, 0);

    phys_compound_add_child(NULL, NULL, NULL, 0);

    phys_compound_collider_t cc;
    phys_compound_child_t storage[2];
    phys_compound_init(&cc, storage, 2);

    /* NULL collider in add_child. */
    phys_compound_add_child(&cc, NULL, NULL, 0);
    ASSERT_INT_EQ(0, cc.child_count);

    /* NULL bone arrays in update_transforms. */
    phys_compound_update_transforms(NULL, NULL, NULL, 0);
    phys_compound_update_transforms(&cc, NULL, NULL, 0);

    /* NULL output in compute_aabb. */
    phys_compound_compute_aabb(NULL, ZERO_VEC3, IDENTITY_QUAT, NULL);
    phys_compound_compute_aabb(&cc, ZERO_VEC3, IDENTITY_QUAT, NULL);

    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"compound_init",                  test_compound_init},
    {"compound_add_sphere_child",      test_compound_add_sphere_child},
    {"compound_add_box_child",         test_compound_add_box_child},
    {"compound_add_capsule_child",     test_compound_add_capsule_child},
    {"compound_add_at_capacity",       test_compound_add_at_capacity},
    {"compound_bone_update",           test_compound_bone_update},
    {"compound_static_bone_unchanged", test_compound_static_bone_unchanged},
    {"compound_aabb_single_sphere",    test_compound_aabb_single_sphere},
    {"compound_aabb_two_children",     test_compound_aabb_two_children},
    {"compound_aabb_rotated_body",     test_compound_aabb_rotated_body},
    {"compound_null_safe",             test_compound_null_safe},
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
