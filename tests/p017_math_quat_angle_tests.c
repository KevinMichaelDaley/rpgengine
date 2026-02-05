#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/math/quat_angle.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

static int nearly_equalf(float a, float b, float eps) {
    const float d = fabsf(a - b);
    return d <= eps;
}

static int test_angle_between_same_is_zero(void) {
    quat_t q = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    const float deg = fr_quat_angle_degrees_between(q, q);
    ASSERT_TRUE(nearly_equalf(deg, 0.0f, 1e-4f));
    return 0;
}

static int test_angle_between_q_and_neg_q_is_zero(void) {
    quat_t q = (quat_t){0.0f, 0.0f, 0.70710678f, 0.70710678f};
    quat_t nq = (quat_t){-q.x, -q.y, -q.z, -q.w};
    const float deg = fr_quat_angle_degrees_between(q, nq);
    ASSERT_TRUE(nearly_equalf(deg, 0.0f, 1e-3f));
    return 0;
}

static int test_integrate_constant_omega_matches_angle(void) {
    quat_t q = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    const double pi = 3.14159265358979323846;
    vec3_t omega = (vec3_t){0.0f, 0.0f, (float)(pi / 2.0)}; /* 90 deg/s around +Z */

    quat_t q1 = fr_quat_integrate_angular_velocity(q, omega, 1.0f, 1e-6f);
    const float deg = fr_quat_angle_degrees_between(q, q1);
    ASSERT_TRUE(nearly_equalf(deg, 90.0f, 0.25f));
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"angle_between_same_is_zero", test_angle_between_same_is_zero},
    {"angle_between_q_and_neg_q_is_zero", test_angle_between_q_and_neg_q_is_zero},
    {"integrate_constant_omega_matches_angle", test_integrate_constant_omega_matches_angle},
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0u;

    for (size_t i = 0u; i < total; ++i) {
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
