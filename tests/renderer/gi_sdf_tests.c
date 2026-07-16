/**
 * @file gi_sdf_tests.c
 * @brief Unit tests for the combined dynamic SDF: analytic collider SDFs
 *        (sphere/box/capsule) min-combined with a trilinearly-sampled baked
 *        distance field (rpg-d1ok).
 */
#include <math.h>
#include <stdio.h>

#include "ferrum/renderer/gi/gi_sdf.h"

#define ASSERT_NEAR(exp, act, eps)                                           \
    do {                                                                     \
        float _e = (exp), _a = (act);                                        \
        if (fabsf(_e - _a) > (eps)) {                                        \
            printf("  FAIL %s:%d: |%.4f - %.4f| > %.4f\n", __FILE__,         \
                   __LINE__, _e, _a, (float)(eps));                          \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_TRUE(cond)                                                    \
    do { if (!(cond)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                               #cond); return 1; } } while (0)

static int test_sphere(void)
{
    gi_collider_t s = { GI_COLLIDER_SPHERE, { 0, 0, 0 }, { 0, 0, 0 }, { 1, 0, 0 } };
    float c[3] = { 0, 0, 0 }, surf[3] = { 1, 0, 0 }, out[3] = { 3, 0, 0 };
    ASSERT_NEAR(-1.0f, gi_collider_distance(&s, c), 1e-5f);
    ASSERT_NEAR(0.0f, gi_collider_distance(&s, surf), 1e-5f);
    ASSERT_NEAR(2.0f, gi_collider_distance(&s, out), 1e-5f);
    return 0;
}

static int test_box(void)
{
    gi_collider_t b = { GI_COLLIDER_BOX, { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1 } };
    float c[3] = { 0, 0, 0 }, face[3] = { 1, 0, 0 }, out[3] = { 3, 0, 0 };
    ASSERT_NEAR(-1.0f, gi_collider_distance(&b, c), 1e-5f);    /* deepest inside. */
    ASSERT_NEAR(0.0f, gi_collider_distance(&b, face), 1e-5f);
    ASSERT_NEAR(2.0f, gi_collider_distance(&b, out), 1e-5f);
    return 0;
}

static int test_capsule(void)
{
    /* segment (0,0,0)-(0,0,2), radius 0.5. */
    gi_collider_t k = { GI_COLLIDER_CAPSULE, { 0, 0, 0 }, { 0, 0, 2 }, { 0.5f, 0, 0 } };
    float mid[3] = { 0, 0, 1 }, side[3] = { 0.5f, 0, 1 }, out[3] = { 1.5f, 0, 1 };
    ASSERT_NEAR(-0.5f, gi_collider_distance(&k, mid), 1e-5f);
    ASSERT_NEAR(0.0f, gi_collider_distance(&k, side), 1e-5f);
    ASSERT_NEAR(1.0f, gi_collider_distance(&k, out), 1e-5f);
    return 0;
}

static int test_baked_sample(void)
{
    /* 2x2x2 field, voxel 1, origin 0. dist[x + 2y + 4z]. */
    float dist[8] = { 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    int32_t dims[3] = { 2, 2, 2 };
    float origin[3] = { 0, 0, 0 };

    float p0[3] = { 0, 0, 0 }, p1[3] = { 1, 0, 0 }, pm[3] = { 0.5f, 0, 0 };
    ASSERT_NEAR(0.0f, gi_sdf_baked_sample(dist, dims, origin, 1.0f, p0), 1e-5f);
    ASSERT_NEAR(2.0f, gi_sdf_baked_sample(dist, dims, origin, 1.0f, p1), 1e-5f);
    ASSERT_NEAR(1.0f, gi_sdf_baked_sample(dist, dims, origin, 1.0f, pm), 1e-5f);

    /* Outside the grid -> large positive (far, no occlusion). */
    float outside[3] = { 9, 9, 9 };
    ASSERT_TRUE(gi_sdf_baked_sample(dist, dims, origin, 1.0f, outside) > 1e6f);
    return 0;
}

static int test_combined(void)
{
    /* Baked field far everywhere; a sphere dominates near its centre. */
    float dist[8]; for (int i = 0; i < 8; ++i) dist[i] = 10.0f;
    int32_t dims[3] = { 2, 2, 2 };
    float origin[3] = { 0, 0, 0 };
    gi_collider_t s = { GI_COLLIDER_SPHERE, { 0.5f, 0.5f, 0.5f }, { 0, 0, 0 }, { 0.25f, 0, 0 } };

    float p[3] = { 0.5f, 0.5f, 0.5f };
    /* min(baked=10, sphere=-0.25) = -0.25. */
    ASSERT_NEAR(-0.25f, gi_sdf_combined(dist, dims, origin, 1.0f, &s, 1u, p), 1e-5f);
    /* With no colliders it is just the baked sample. */
    ASSERT_NEAR(10.0f, gi_sdf_combined(dist, dims, origin, 1.0f, NULL, 0u, p), 1e-5f);
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_sphere();
    rc |= test_box();
    rc |= test_capsule();
    rc |= test_baked_sample();
    rc |= test_combined();
    if (rc == 0)
        printf("  OK: all gi_sdf tests passed\n");
    return rc;
}
