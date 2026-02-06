#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/math/constants.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"
#include "ferrum/physics/collider.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                \
    do {                                                                        \
        if ((exp) != (act)) {                                                   \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n",\
                    __FILE__, __LINE__, (int)(exp), (int)(act));                \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                       \
    do {                                                                        \
        float _e = (float)(exp);                                                \
        float _a = (float)(act);                                                \
        if (fabsf(_e - _a) > (eps)) {                                           \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "                \
                    "expected %f got %f (eps=%f)\n",                            \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)(eps)); \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_VEC3_NEAR(exp, act, eps)                                        \
    do {                                                                        \
        ASSERT_FLOAT_NEAR((exp).x, (act).x, (eps));                            \
        ASSERT_FLOAT_NEAR((exp).y, (act).y, (eps));                            \
        ASSERT_FLOAT_NEAR((exp).z, (act).z, (eps));                            \
    } while (0)

/* ── Struct size tests ──────────────────────────────────────────── */

static int test_sphere_size(void) {
    ASSERT_INT_EQ(4, (int)sizeof(phys_sphere_t));
    return 0;
}

static int test_box_size(void) {
    ASSERT_INT_EQ(12, (int)sizeof(phys_box_t));
    return 0;
}

static int test_capsule_size(void) {
    ASSERT_INT_EQ(8, (int)sizeof(phys_capsule_t));
    return 0;
}

static int test_collider_size(void) {
    /* phys_collider_t: type(4) + shape_index(4) + offset(12) + rotation(16)
     *                + sphere_simplify(1) + pad(3) = 40 bytes */
    ASSERT_INT_EQ(40, (int)sizeof(phys_collider_t));
    return 0;
}

/* ── Init tests ─────────────────────────────────────────────────── */

static int test_sphere_collider_init(void) {
    phys_collider_t c;
    phys_collider_init_sphere(&c, 42, (phys_vec3_t){1, 0, 0});
    ASSERT_INT_EQ(PHYS_SHAPE_SPHERE, (int)c.type);
    ASSERT_INT_EQ(42, (int)c.shape_index);
    ASSERT_VEC3_NEAR(((phys_vec3_t){1, 0, 0}), c.local_offset, 1e-6f);
    /* Sphere init sets identity rotation. */
    ASSERT_FLOAT_NEAR(0.0f, c.local_rotation.x, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, c.local_rotation.y, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, c.local_rotation.z, 1e-6f);
    ASSERT_FLOAT_NEAR(1.0f, c.local_rotation.w, 1e-6f);
    ASSERT_INT_EQ(0, (int)c.sphere_simplify);
    return 0;
}

static int test_box_collider_init(void) {
    phys_quat_t rot = quat_from_axis_angle(
        (vec3_t){0, 1, 0}, FERRUM_PI / 4.0f, 1e-6f);
    phys_collider_t c;
    phys_collider_init_box(&c, 10, (phys_vec3_t){0, 0, 0}, rot);
    ASSERT_INT_EQ(PHYS_SHAPE_BOX, (int)c.type);
    ASSERT_INT_EQ(10, (int)c.shape_index);
    /* Non-identity rotation stored. */
    ASSERT_TRUE(fabsf(c.local_rotation.w - 1.0f) > 0.01f);
    return 0;
}

static int test_capsule_collider_init(void) {
    phys_quat_t identity = {0, 0, 0, 1};
    phys_collider_t c;
    phys_collider_init_capsule(&c, 7, (phys_vec3_t){0, 2, 0}, identity);
    ASSERT_INT_EQ(PHYS_SHAPE_CAPSULE, (int)c.type);
    ASSERT_INT_EQ(7, (int)c.shape_index);
    ASSERT_FLOAT_NEAR(2.0f, c.local_offset.y, 1e-6f);
    return 0;
}

/* ── Null safety tests ──────────────────────────────────────────── */

static int test_init_null_safe(void) {
    /* These should not crash. */
    phys_collider_init_sphere(NULL, 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_box(NULL, 0, (phys_vec3_t){0, 0, 0},
                           (phys_quat_t){0, 0, 0, 1});
    phys_collider_init_capsule(NULL, 0, (phys_vec3_t){0, 0, 0},
                               (phys_quat_t){0, 0, 0, 1});
    return 0;
}

/* ── World transform tests ──────────────────────────────────────── */

static int test_world_center_identity(void) {
    /* Collider offset (1,0,0) with identity body rotation at (10,0,0). */
    phys_collider_t c;
    phys_collider_init_sphere(&c, 0, (phys_vec3_t){1, 0, 0});

    phys_vec3_t body_pos = {10, 0, 0};
    phys_quat_t body_rot = {0, 0, 0, 1};

    phys_vec3_t world = phys_collider_world_center(&c, body_pos, body_rot);
    ASSERT_VEC3_NEAR(((phys_vec3_t){11, 0, 0}), world, 1e-5f);
    return 0;
}

static int test_world_center_rotated_body(void) {
    /* Collider offset (1,0,0), body at (10,0,0), body rotated 90° around Z.
     * Offset (1,0,0) rotated 90° around Z → (0,1,0).
     * World center = (10,0,0) + (0,1,0) = (10,1,0). */
    phys_collider_t c;
    phys_collider_init_sphere(&c, 0, (phys_vec3_t){1, 0, 0});

    phys_vec3_t body_pos = {10, 0, 0};
    phys_quat_t body_rot = quat_from_axis_angle(
        (vec3_t){0, 0, 1}, FERRUM_PI / 2.0f, 1e-6f);

    phys_vec3_t world = phys_collider_world_center(&c, body_pos, body_rot);
    ASSERT_VEC3_NEAR(((phys_vec3_t){10, 1, 0}), world, 1e-3f);
    return 0;
}

static int test_world_center_zero_offset(void) {
    /* No offset → world center equals body position. */
    phys_collider_t c;
    phys_collider_init_sphere(&c, 0, (phys_vec3_t){0, 0, 0});

    phys_vec3_t body_pos = {5, 3, -1};
    phys_quat_t body_rot = quat_from_axis_angle(
        (vec3_t){1, 0, 0}, 1.234f, 1e-6f);

    phys_vec3_t world = phys_collider_world_center(&c, body_pos, body_rot);
    ASSERT_VEC3_NEAR(body_pos, world, 1e-5f);
    return 0;
}

static int test_world_center_null_safe(void) {
    /* NULL collider → returns body_pos unchanged. */
    phys_vec3_t body_pos = {5, 3, -1};
    phys_quat_t body_rot = {0, 0, 0, 1};
    phys_vec3_t world = phys_collider_world_center(NULL, body_pos, body_rot);
    ASSERT_VEC3_NEAR(body_pos, world, 1e-6f);
    return 0;
}

static int test_world_rotation_identity(void) {
    /* Identity body + identity local → identity world. */
    phys_collider_t c;
    phys_collider_init_sphere(&c, 0, (phys_vec3_t){0, 0, 0});
    phys_quat_t body_rot = {0, 0, 0, 1};
    phys_quat_t world_rot = phys_collider_world_rotation(&c, body_rot);
    ASSERT_FLOAT_NEAR(0.0f, world_rot.x, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, world_rot.y, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, world_rot.z, 1e-6f);
    ASSERT_FLOAT_NEAR(1.0f, world_rot.w, 1e-6f);
    return 0;
}

static int test_world_rotation_combined(void) {
    /* Body rotated 90° around Y, collider local rotated 90° around X.
     * Combined world rotation = body_rot * local_rot. */
    phys_quat_t body_rot = quat_from_axis_angle(
        (vec3_t){0, 1, 0}, FERRUM_PI / 2.0f, 1e-6f);
    phys_quat_t local_rot = quat_from_axis_angle(
        (vec3_t){1, 0, 0}, FERRUM_PI / 2.0f, 1e-6f);

    phys_collider_t c;
    phys_collider_init_box(&c, 0, (phys_vec3_t){0, 0, 0}, local_rot);

    phys_quat_t world_rot = phys_collider_world_rotation(&c, body_rot);

    /* The combined rotation should be a unit quaternion. */
    float len_sq = world_rot.x * world_rot.x + world_rot.y * world_rot.y +
                   world_rot.z * world_rot.z + world_rot.w * world_rot.w;
    ASSERT_FLOAT_NEAR(1.0f, len_sq, 1e-4f);

    /* The combined rotation (body_rot * local_rot) applied to (0,0,1):
     *   local_rot(90° X) maps (0,0,1) → (0,-1,0)
     *   body_rot(90° Y) maps (0,-1,0) → (0,-1,0)  (Y axis unchanged by Y rotation)
     * So combined * (0,0,1) ≈ (0,-1,0). */
    phys_collider_t test_c;
    phys_collider_init_sphere(&test_c, 0, (phys_vec3_t){0, 0, 1});
    /* Rotate offset by the combined quaternion to verify. */
    phys_vec3_t result = phys_collider_world_center(
        &test_c, (phys_vec3_t){0, 0, 0}, world_rot);
    ASSERT_VEC3_NEAR(((phys_vec3_t){0, -1, 0}), result, 1e-3f);
    return 0;
}

static int test_world_rotation_null_safe(void) {
    phys_quat_t body_rot = {0, 0, 0, 1};
    phys_quat_t result = phys_collider_world_rotation(NULL, body_rot);
    ASSERT_FLOAT_NEAR(1.0f, result.w, 1e-6f);
    return 0;
}

/* ── Shape enum test ────────────────────────────────────────────── */

static int test_shape_type_enum(void) {
    ASSERT_INT_EQ(0, PHYS_SHAPE_SPHERE);
    ASSERT_INT_EQ(1, PHYS_SHAPE_BOX);
    ASSERT_INT_EQ(2, PHYS_SHAPE_CAPSULE);
    ASSERT_INT_EQ(3, PHYS_SHAPE_COMPOUND);
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"sphere_size",                 test_sphere_size},
    {"box_size",                    test_box_size},
    {"capsule_size",                test_capsule_size},
    {"collider_size",               test_collider_size},
    {"sphere_collider_init",        test_sphere_collider_init},
    {"box_collider_init",           test_box_collider_init},
    {"capsule_collider_init",       test_capsule_collider_init},
    {"init_null_safe",              test_init_null_safe},
    {"world_center_identity",       test_world_center_identity},
    {"world_center_rotated_body",   test_world_center_rotated_body},
    {"world_center_zero_offset",    test_world_center_zero_offset},
    {"world_center_null_safe",      test_world_center_null_safe},
    {"world_rotation_identity",     test_world_rotation_identity},
    {"world_rotation_combined",     test_world_rotation_combined},
    {"world_rotation_null_safe",    test_world_rotation_null_safe},
    {"shape_type_enum",             test_shape_type_enum},
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
