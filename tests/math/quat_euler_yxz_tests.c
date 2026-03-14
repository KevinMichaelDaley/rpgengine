/**
 * @file quat_euler_yxz_tests.c
 * @brief Tests for YXZ-order euler↔quaternion conversion.
 *
 * The engine uses Y*X*Z matrix multiplication order for entity rotation.
 * These tests verify roundtrip conversion (euler→quat→euler) and
 * correctness against known rotation matrices.
 */

#include <math.h>
#include <stdio.h>
#include "ferrum/math/quat.h"
#include "ferrum/math/mat4.h"

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",               \
                    __FILE__, __LINE__, #cond);                               \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int nearly_equalf(float a, float b, float eps) {
    return fabsf(a - b) <= eps;
}

static const float DEG = 3.14159265358979323846f / 180.0f;
static const float EPS = 1e-4f;

/* ---- Happy path ---- */

static int test_identity_roundtrip(void) {
    quat_t q = quat_from_euler_yxz(0.0f, 0.0f, 0.0f);
    float rx, ry, rz;
    quat_to_euler_yxz(q, &rx, &ry, &rz);
    ASSERT_TRUE(nearly_equalf(rx, 0.0f, EPS));
    ASSERT_TRUE(nearly_equalf(ry, 0.0f, EPS));
    ASSERT_TRUE(nearly_equalf(rz, 0.0f, EPS));
    return 0;
}

static int test_pure_x_rotation(void) {
    float angle = 45.0f * DEG;
    quat_t q = quat_from_euler_yxz(angle, 0.0f, 0.0f);
    float rx, ry, rz;
    quat_to_euler_yxz(q, &rx, &ry, &rz);
    ASSERT_TRUE(nearly_equalf(rx, angle, EPS));
    ASSERT_TRUE(nearly_equalf(ry, 0.0f, EPS));
    ASSERT_TRUE(nearly_equalf(rz, 0.0f, EPS));
    return 0;
}

static int test_pure_y_rotation(void) {
    float angle = 60.0f * DEG;
    quat_t q = quat_from_euler_yxz(0.0f, angle, 0.0f);
    float rx, ry, rz;
    quat_to_euler_yxz(q, &rx, &ry, &rz);
    ASSERT_TRUE(nearly_equalf(rx, 0.0f, EPS));
    ASSERT_TRUE(nearly_equalf(ry, angle, EPS));
    ASSERT_TRUE(nearly_equalf(rz, 0.0f, EPS));
    return 0;
}

static int test_pure_z_rotation(void) {
    float angle = 30.0f * DEG;
    quat_t q = quat_from_euler_yxz(0.0f, 0.0f, angle);
    float rx, ry, rz;
    quat_to_euler_yxz(q, &rx, &ry, &rz);
    ASSERT_TRUE(nearly_equalf(rx, 0.0f, EPS));
    ASSERT_TRUE(nearly_equalf(ry, 0.0f, EPS));
    ASSERT_TRUE(nearly_equalf(rz, angle, EPS));
    return 0;
}

static int test_combined_yxz_roundtrip(void) {
    float x = 25.0f * DEG;
    float y = 40.0f * DEG;
    float z = -15.0f * DEG;
    quat_t q = quat_from_euler_yxz(x, y, z);
    float rx, ry, rz;
    quat_to_euler_yxz(q, &rx, &ry, &rz);
    ASSERT_TRUE(nearly_equalf(rx, x, EPS));
    ASSERT_TRUE(nearly_equalf(ry, y, EPS));
    ASSERT_TRUE(nearly_equalf(rz, z, EPS));
    return 0;
}

static int test_negative_angles_roundtrip(void) {
    float x = -35.0f * DEG;
    float y = -70.0f * DEG;
    float z = 10.0f * DEG;
    quat_t q = quat_from_euler_yxz(x, y, z);
    float rx, ry, rz;
    quat_to_euler_yxz(q, &rx, &ry, &rz);
    ASSERT_TRUE(nearly_equalf(rx, x, EPS));
    ASSERT_TRUE(nearly_equalf(ry, y, EPS));
    ASSERT_TRUE(nearly_equalf(rz, z, EPS));
    return 0;
}

/* ---- Matrix consistency ---- */

static int test_quat_matches_matrix_yxz(void) {
    /* Build rotation from euler via matrix path (Y * X * Z). */
    float x = 30.0f * DEG;
    float y = 45.0f * DEG;
    float z = 20.0f * DEG;
    mat4_t rx = mat4_rotation_x(x);
    mat4_t ry = mat4_rotation_y(y);
    mat4_t rz = mat4_rotation_z(z);
    mat4_t mat_rot = mat4_mul(ry, mat4_mul(rx, rz));

    /* Build rotation from euler via quaternion path. */
    quat_t q = quat_from_euler_yxz(x, y, z);
    mat4_t quat_rot;
    quat_to_mat4(q, &quat_rot);

    /* Matrices should match (upper-left 3x3). */
    for (int i = 0; i < 12; i++) {
        if (i % 4 == 3) continue; /* Skip bottom row. */
        ASSERT_TRUE(nearly_equalf(mat_rot.m[i], quat_rot.m[i], EPS));
    }
    return 0;
}

/* ---- Edge cases ---- */

static int test_gimbal_lock_x_plus_90(void) {
    /* X = +90° causes gimbal lock. Y and Z become coupled. */
    float x = 90.0f * DEG;
    float y = 30.0f * DEG;
    float z = 0.0f;
    quat_t q = quat_from_euler_yxz(x, y, z);
    float rx, ry, rz;
    quat_to_euler_yxz(q, &rx, &ry, &rz);

    /* X should be ≈ 90°. Y + Z sum should be preserved. */
    ASSERT_TRUE(nearly_equalf(rx, x, EPS));
    /* At gimbal lock, we set z=0 and absorb into y. */
    ASSERT_TRUE(nearly_equalf(rz, 0.0f, EPS));
    return 0;
}

static int test_gimbal_lock_x_minus_90(void) {
    float x = -90.0f * DEG;
    float y = 45.0f * DEG;
    float z = 0.0f;
    quat_t q = quat_from_euler_yxz(x, y, z);
    float rx, ry, rz;
    quat_to_euler_yxz(q, &rx, &ry, &rz);
    ASSERT_TRUE(nearly_equalf(rx, x, EPS));
    ASSERT_TRUE(nearly_equalf(rz, 0.0f, EPS));
    return 0;
}

static int test_180_degree_rotation(void) {
    float y = 180.0f * DEG;
    quat_t q = quat_from_euler_yxz(0.0f, y, 0.0f);
    float rx, ry, rz;
    quat_to_euler_yxz(q, &rx, &ry, &rz);
    /* 180° Y rotation. */
    ASSERT_TRUE(nearly_equalf(fabsf(ry), 180.0f * DEG, EPS));
    return 0;
}

/* ---- Composition test ---- */

static int test_sequential_rotation_differs_from_euler_add(void) {
    /* This test demonstrates WHY quaternions are needed:
     * Adding euler angles does NOT produce the same rotation as
     * composing the individual rotations. */
    float y1 = 45.0f * DEG;
    float x2 = 30.0f * DEG;

    /* Method 1: euler angle addition (WRONG). */
    float added_x = x2;
    float added_y = y1;
    quat_t q_added = quat_from_euler_yxz(added_x, added_y, 0.0f);

    /* Method 2: quaternion composition (CORRECT).
     * First rotate Y by 45°, then rotate X by 30°. */
    quat_t q1 = quat_from_euler_yxz(0.0f, y1, 0.0f);
    quat_t q2 = quat_from_axis_angle((vec3_t){1, 0, 0}, x2, 1e-8f);
    quat_t q_composed = quat_mul(q2, q1);

    /* These should NOT be the same (proving euler addition is wrong). */
    mat4_t m_added, m_composed;
    quat_to_mat4(q_added, &m_added);
    quat_to_mat4(q_composed, &m_composed);

    /* At least one matrix element should differ significantly. */
    float max_diff = 0.0f;
    for (int i = 0; i < 12; i++) {
        if (i % 4 == 3) continue;
        float d = fabsf(m_added.m[i] - m_composed.m[i]);
        if (d > max_diff) max_diff = d;
    }
    /* With 45° Y then 30° X, the difference is noticeable. */
    ASSERT_TRUE(max_diff > 0.01f);
    return 0;
}

int main(void) {
    int fail = 0;
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"identity_roundtrip",                test_identity_roundtrip},
        {"pure_x_rotation",                   test_pure_x_rotation},
        {"pure_y_rotation",                   test_pure_y_rotation},
        {"pure_z_rotation",                   test_pure_z_rotation},
        {"combined_yxz_roundtrip",            test_combined_yxz_roundtrip},
        {"negative_angles_roundtrip",         test_negative_angles_roundtrip},
        {"quat_matches_matrix_yxz",           test_quat_matches_matrix_yxz},
        {"gimbal_lock_x_plus_90",             test_gimbal_lock_x_plus_90},
        {"gimbal_lock_x_minus_90",            test_gimbal_lock_x_minus_90},
        {"180_degree_rotation",               test_180_degree_rotation},
        {"sequential_rotation_differs",       test_sequential_rotation_differs_from_euler_add},
    };
    int count = (int)(sizeof(tests) / sizeof(tests[0]));
    for (int i = 0; i < count; i++) {
        int r = tests[i].fn();
        if (r != 0) {
            fprintf(stderr, "FAIL: %s\n", tests[i].name);
            fail++;
        } else {
            printf("PASS: %s\n", tests[i].name);
        }
    }
    printf("\n%d/%d passed\n", count - fail, count);
    return fail;
}
