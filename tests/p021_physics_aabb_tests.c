/**
 * @file p021_physics_aabb_tests.c
 * @brief Unit tests for AABB structure and computation (phys-004).
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "ferrum/math/constants.h"
#include "ferrum/math/quat.h"
#include "ferrum/physics/aabb.h"

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

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_aabb_size(void) {
    ASSERT_INT_EQ(24, (int)sizeof(phys_aabb_t));
    return 0;
}

static int test_aabb_sphere(void) {
    phys_aabb_t aabb;
    phys_aabb_from_sphere(&aabb, (phys_vec3_t){5.0f, 10.0f, 15.0f}, 2.0f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){3.0f, 8.0f, 13.0f}), aabb.min, 1e-6f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){7.0f, 12.0f, 17.0f}), aabb.max, 1e-6f);
    return 0;
}

static int test_aabb_box_axis_aligned(void) {
    phys_aabb_t aabb;
    phys_quat_t identity = {0.0f, 0.0f, 0.0f, 1.0f};
    phys_aabb_from_box(&aabb, (phys_vec3_t){0.0f, 0.0f, 0.0f}, identity,
                       (phys_vec3_t){1.0f, 2.0f, 3.0f});
    ASSERT_VEC3_NEAR(((phys_vec3_t){-1.0f, -2.0f, -3.0f}), aabb.min, 1e-6f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, 2.0f, 3.0f}), aabb.max, 1e-6f);
    return 0;
}

static int test_aabb_box_rotated_45(void) {
    phys_aabb_t aabb;
    /* 45° rotation around Y axis with half-extents (2, 0.5, 0.5). */
    phys_quat_t rot45 = quat_from_axis_angle((vec3_t){0.0f, 1.0f, 0.0f},
                                              FERRUM_PI / 4.0f, 1e-6f);
    phys_aabb_from_box(&aabb, (phys_vec3_t){0.0f, 0.0f, 0.0f}, rot45,
                       (phys_vec3_t){2.0f, 0.5f, 0.5f});

    /* X extent should be larger than 2 due to rotation. */
    ASSERT_TRUE(aabb.max.x > 1.4f);
    /* Z extent should be larger than 0.5 due to rotation. */
    ASSERT_TRUE(aabb.max.z > 0.5f);
    /* Y should be unchanged at 0.5. */
    ASSERT_FLOAT_NEAR(0.5f, aabb.max.y, 1e-4f);
    return 0;
}

static int test_aabb_box_rotated_90(void) {
    phys_aabb_t aabb;
    /* 90° rotation around Y axis with half-extents (2, 1, 0.5).
     * After rotation: X and Z swap, so world extents are (0.5, 1, 2). */
    phys_quat_t rot90 = quat_from_axis_angle((vec3_t){0.0f, 1.0f, 0.0f},
                                              FERRUM_PI / 2.0f, 1e-6f);
    phys_aabb_from_box(&aabb, (phys_vec3_t){0.0f, 0.0f, 0.0f}, rot90,
                       (phys_vec3_t){2.0f, 1.0f, 0.5f});

    /* After 90° around Y: local X -> world -Z, local Z -> world X. */
    ASSERT_FLOAT_NEAR(0.5f, aabb.max.x, 1e-3f);
    ASSERT_FLOAT_NEAR(1.0f, aabb.max.y, 1e-3f);
    ASSERT_FLOAT_NEAR(2.0f, aabb.max.z, 1e-3f);
    return 0;
}

static int test_aabb_capsule_vertical(void) {
    phys_aabb_t aabb;
    phys_quat_t identity = {0.0f, 0.0f, 0.0f, 1.0f};
    /* Capsule aligned with +Y, r=0.5, half_height=1.0.
     * Y range: [-1.0 - 0.5, 1.0 + 0.5] = [-1.5, 1.5]
     * X,Z range: [-0.5, 0.5] */
    phys_aabb_from_capsule(&aabb, (phys_vec3_t){0.0f, 0.0f, 0.0f}, identity,
                           0.5f, 1.0f);
    ASSERT_FLOAT_NEAR(-1.5f, aabb.min.y, 1e-4f);
    ASSERT_FLOAT_NEAR(1.5f, aabb.max.y, 1e-4f);
    ASSERT_FLOAT_NEAR(-0.5f, aabb.min.x, 1e-4f);
    ASSERT_FLOAT_NEAR(0.5f, aabb.max.x, 1e-4f);
    ASSERT_FLOAT_NEAR(-0.5f, aabb.min.z, 1e-4f);
    ASSERT_FLOAT_NEAR(0.5f, aabb.max.z, 1e-4f);
    return 0;
}

static int test_aabb_capsule_horizontal(void) {
    phys_aabb_t aabb;
    /* 90° around Z rotates +Y -> -X. */
    phys_quat_t rot90z = quat_from_axis_angle((vec3_t){0.0f, 0.0f, 1.0f},
                                               FERRUM_PI / 2.0f, 1e-6f);
    phys_aabb_from_capsule(&aabb, (phys_vec3_t){0.0f, 0.0f, 0.0f}, rot90z,
                           0.5f, 1.0f);
    /* Now aligned with X: X range [-1.5, 1.5], Y and Z range [-0.5, 0.5]. */
    ASSERT_FLOAT_NEAR(-1.5f, aabb.min.x, 1e-3f);
    ASSERT_FLOAT_NEAR(1.5f, aabb.max.x, 1e-3f);
    ASSERT_FLOAT_NEAR(-0.5f, aabb.min.y, 1e-3f);
    ASSERT_FLOAT_NEAR(0.5f, aabb.max.y, 1e-3f);
    return 0;
}

static int test_aabb_overlap_true(void) {
    phys_aabb_t a = {{0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}};
    phys_aabb_t b = {{1.0f, 1.0f, 1.0f}, {3.0f, 3.0f, 3.0f}};
    ASSERT_TRUE(phys_aabb_overlap(&a, &b));
    return 0;
}

static int test_aabb_overlap_false(void) {
    phys_aabb_t a = {{0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}};
    phys_aabb_t c = {{5.0f, 5.0f, 5.0f}, {6.0f, 6.0f, 6.0f}};
    ASSERT_TRUE(!phys_aabb_overlap(&a, &c));
    return 0;
}

static int test_aabb_overlap_touching(void) {
    /* Edge-touching counts as overlap. */
    phys_aabb_t a = {{0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}};
    phys_aabb_t d = {{2.0f, 0.0f, 0.0f}, {4.0f, 2.0f, 2.0f}};
    ASSERT_TRUE(phys_aabb_overlap(&a, &d));
    return 0;
}

static int test_aabb_merge(void) {
    phys_aabb_t a = {{0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}};
    phys_aabb_t c = {{5.0f, 5.0f, 5.0f}, {6.0f, 6.0f, 6.0f}};
    phys_aabb_t merged;
    phys_aabb_merge(&merged, &a, &c);
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, 0.0f, 0.0f}), merged.min, 1e-6f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){6.0f, 6.0f, 6.0f}), merged.max, 1e-6f);
    return 0;
}

static int test_aabb_expand(void) {
    phys_aabb_t a = {{0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}};
    phys_aabb_expand(&a, 0.5f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){-0.5f, -0.5f, -0.5f}), a.min, 1e-6f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){2.5f, 2.5f, 2.5f}), a.max, 1e-6f);
    return 0;
}

static int test_aabb_center(void) {
    phys_aabb_t a = {{1.0f, 2.0f, 3.0f}, {5.0f, 8.0f, 9.0f}};
    phys_vec3_t c = phys_aabb_center(&a);
    ASSERT_VEC3_NEAR(((phys_vec3_t){3.0f, 5.0f, 6.0f}), c, 1e-6f);
    return 0;
}

static int test_aabb_extents(void) {
    phys_aabb_t a = {{1.0f, 2.0f, 3.0f}, {5.0f, 8.0f, 9.0f}};
    phys_vec3_t e = phys_aabb_extents(&a);
    ASSERT_VEC3_NEAR(((phys_vec3_t){4.0f, 6.0f, 6.0f}), e, 1e-6f);
    return 0;
}

static int test_aabb_surface_area(void) {
    /* Box 4×6×6: SA = 2*(4*6 + 4*6 + 6*6) = 2*(24+24+36) = 168 */
    phys_aabb_t a = {{1.0f, 2.0f, 3.0f}, {5.0f, 8.0f, 9.0f}};
    float sa = phys_aabb_surface_area(&a);
    ASSERT_FLOAT_NEAR(168.0f, sa, 1e-4f);
    return 0;
}

static int test_aabb_null_safe(void) {
    /* All functions must handle NULL pointers without crashing. */
    phys_aabb_from_sphere(NULL, (phys_vec3_t){0, 0, 0}, 1.0f);
    phys_aabb_from_box(NULL, (phys_vec3_t){0, 0, 0},
                       (phys_quat_t){0, 0, 0, 1}, (phys_vec3_t){1, 1, 1});
    phys_aabb_from_capsule(NULL, (phys_vec3_t){0, 0, 0},
                           (phys_quat_t){0, 0, 0, 1}, 0.5f, 1.0f);
    ASSERT_TRUE(!phys_aabb_overlap(NULL, NULL));

    phys_aabb_t dummy = {{0, 0, 0}, {1, 1, 1}};
    ASSERT_TRUE(!phys_aabb_overlap(&dummy, NULL));
    ASSERT_TRUE(!phys_aabb_overlap(NULL, &dummy));

    phys_aabb_merge(NULL, &dummy, &dummy);
    phys_aabb_merge(&dummy, NULL, &dummy);
    phys_aabb_merge(&dummy, &dummy, NULL);

    phys_aabb_expand(NULL, 1.0f);

    phys_vec3_t c = phys_aabb_center(NULL);
    ASSERT_FLOAT_NEAR(0.0f, c.x, 1e-6f);

    phys_vec3_t e = phys_aabb_extents(NULL);
    ASSERT_FLOAT_NEAR(0.0f, e.x, 1e-6f);

    float sa = phys_aabb_surface_area(NULL);
    ASSERT_FLOAT_NEAR(0.0f, sa, 1e-6f);

    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"aabb_size",              test_aabb_size},
    {"aabb_sphere",            test_aabb_sphere},
    {"aabb_box_axis_aligned",  test_aabb_box_axis_aligned},
    {"aabb_box_rotated_45",    test_aabb_box_rotated_45},
    {"aabb_box_rotated_90",    test_aabb_box_rotated_90},
    {"aabb_capsule_vertical",  test_aabb_capsule_vertical},
    {"aabb_capsule_horizontal", test_aabb_capsule_horizontal},
    {"aabb_overlap_true",      test_aabb_overlap_true},
    {"aabb_overlap_false",     test_aabb_overlap_false},
    {"aabb_overlap_touching",  test_aabb_overlap_touching},
    {"aabb_merge",             test_aabb_merge},
    {"aabb_expand",            test_aabb_expand},
    {"aabb_center",            test_aabb_center},
    {"aabb_extents",           test_aabb_extents},
    {"aabb_surface_area",      test_aabb_surface_area},
    {"aabb_null_safe",         test_aabb_null_safe},
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
