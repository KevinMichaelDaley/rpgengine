#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

#include "ferrum/physics/phys_types.h"

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
                    (int)(exp), (int)(act));                                                             \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                                                 \
    do {                                                                                                 \
        float _e = (float)(exp);                                                                         \
        float _a = (float)(act);                                                                         \
        if (fabsf(_e - _a) > (eps)) {                                                                    \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f got %f (eps=%f)\n", __FILE__,  \
                    __LINE__, (double)_e, (double)_a, (double)(eps));                                    \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

static int vec3_near(vec3_t a, vec3_t b, float eps) {
    return fabsf(a.x - b.x) <= eps && fabsf(a.y - b.y) <= eps && fabsf(a.z - b.z) <= eps;
}

static int quat_near(quat_t a, quat_t b, float eps) {
    return fabsf(a.x - b.x) <= eps && fabsf(a.y - b.y) <= eps && fabsf(a.z - b.z) <= eps &&
           fabsf(a.w - b.w) <= eps;
}

static int test_type_sizes(void) {
    ASSERT_INT_EQ(12, (int)sizeof(phys_vec3_t));
    ASSERT_INT_EQ(16, (int)sizeof(phys_quat_t));
    ASSERT_INT_EQ(36, (int)sizeof(phys_mat3_t));
    return 0;
}

static int test_vec3_conversion_round_trip(void) {
    vec3_t v = {1.0f, 2.0f, 3.0f};
    phys_vec3_t pv = PHYS_VEC3_FROM_VEC3(v);
    vec3_t v2 = VEC3_FROM_PHYS_VEC3(pv);
    ASSERT_TRUE(vec3_near(v, v2, 0.0f));
    return 0;
}

static int test_quat_conversion_round_trip(void) {
    quat_t q = {1.0f, 2.0f, 3.0f, 4.0f};
    phys_quat_t pq = PHYS_QUAT_FROM_QUAT(q);
    quat_t q2 = QUAT_FROM_PHYS_QUAT(pq);
    ASSERT_TRUE(quat_near(q, q2, 0.0f));
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"type_sizes", test_type_sizes},
    {"vec3_conversion_round_trip", test_vec3_conversion_round_trip},
    {"quat_conversion_round_trip", test_quat_conversion_round_trip},
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
