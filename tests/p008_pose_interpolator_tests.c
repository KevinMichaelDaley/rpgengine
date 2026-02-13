#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"
#include "ferrum/net/replication/interp/pose_interpolator.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
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

static int test_pose_interpolator_samples_midpoint(void) {
    fr_pose_interpolator_t interp;
    fr_pose_interpolator_reset(&interp);

    const vec3_t p0 = {0.0f, 0.0f, 0.0f};
    const vec3_t p1 = {1.0f, 0.0f, 0.0f};
    const quat_t qI = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    ASSERT_TRUE(fr_pose_interpolator_push(&interp, 0.0, p0, qI, (vec3_t){0,0,0}, (vec3_t){0,0,0}));
    ASSERT_TRUE(fr_pose_interpolator_push(&interp, 1.0, p1, qI, (vec3_t){0,0,0}, (vec3_t){0,0,0}));

    vec3_t out_p;
    quat_t out_q;
    ASSERT_TRUE(fr_pose_interpolator_sample(&interp, 0.5, 1e-6f, &out_p, &out_q));

    ASSERT_FLOAT_NEAR(0.5f, out_p.x, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, out_p.y, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, out_p.z, 1e-5f);

    ASSERT_FLOAT_NEAR(0.0f, out_q.x, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, out_q.y, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, out_q.z, 1e-5f);
    ASSERT_FLOAT_NEAR(1.0f, out_q.w, 1e-5f);

    return 0;
}

static int test_pose_interpolator_clamps_outside_window(void) {
    fr_pose_interpolator_t interp;
    fr_pose_interpolator_reset(&interp);

    const vec3_t p0 = {-2.0f, 0.0f, 0.0f};
    const vec3_t p1 = {3.0f, 0.0f, 0.0f};
    const quat_t qI = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    ASSERT_TRUE(fr_pose_interpolator_push(&interp, 10.0, p0, qI, (vec3_t){0,0,0}, (vec3_t){0,0,0}));
    ASSERT_TRUE(fr_pose_interpolator_push(&interp, 20.0, p1, qI, (vec3_t){0.5f,0,0}, (vec3_t){0,0,0}));

    vec3_t out_p;
    quat_t out_q;

    /* Before window: clamped to t=0 → p0. */
    ASSERT_TRUE(fr_pose_interpolator_sample(&interp, 9.0, 1e-6f, &out_p, &out_q));
    ASSERT_FLOAT_NEAR(-2.0f, out_p.x, 1e-5f);

    /* Beyond window: extrapolates using server velocity.
     * t = (25-10)/(20-10) = 1.5, extrap = clamp(0.5, 0, 0.5) = 0.5
     * server_vel.x = 0.5, result = 3 + 0.5*(0.5*10) = 5.5 */
    ASSERT_TRUE(fr_pose_interpolator_sample(&interp, 25.0, 1e-6f, &out_p, &out_q));
    ASSERT_FLOAT_NEAR(5.5f, out_p.x, 1e-5f);

    /* Far beyond window: extrap clamped at 0.5 (half-tick cap).
     * t = (50-10)/10 = 4.0, extrap = clamp(3.0, 0, 0.5) = 0.5
     * result = 3 + 0.5*(0.5*10) = 5.5 */
    ASSERT_TRUE(fr_pose_interpolator_sample(&interp, 50.0, 1e-6f, &out_p, &out_q));
    ASSERT_FLOAT_NEAR(5.5f, out_p.x, 1e-5f);

    return 0;
}

static int test_pose_interpolator_rejects_invalid_args(void) {
    fr_pose_interpolator_t interp;
    fr_pose_interpolator_reset(&interp);

    const vec3_t p = {0.0f, 0.0f, 0.0f};
    const quat_t q = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    ASSERT_TRUE(!fr_pose_interpolator_push(NULL, 0.0, p, q, (vec3_t){0,0,0}, (vec3_t){0,0,0}));

    vec3_t out_p;
    quat_t out_q;
    ASSERT_TRUE(!fr_pose_interpolator_sample(NULL, 0.0, 1e-6f, &out_p, &out_q));
    ASSERT_TRUE(!fr_pose_interpolator_sample(&interp, 0.0, 1e-6f, NULL, &out_q));
    ASSERT_TRUE(!fr_pose_interpolator_sample(&interp, 0.0, 1e-6f, &out_p, NULL));

    return 0;
}

static int test_pose_interpolator_server_vel_extrapolation(void) {
    fr_pose_interpolator_t interp;
    fr_pose_interpolator_reset(&interp);

    const vec3_t p0 = {0.0f, 0.0f, 0.0f};
    const vec3_t p1 = {1.0f, 0.0f, 0.0f};
    const quat_t qI = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    /* implied_vel.x would be (1-0)/1 = 1.0, but server says 5.0 m/s.
     * Extrapolation should use server velocity, not implied. */
    ASSERT_TRUE(fr_pose_interpolator_push(&interp, 0.0, p0, qI,
                                          (vec3_t){0,0,0}, (vec3_t){0,0,0}));
    ASSERT_TRUE(fr_pose_interpolator_push(&interp, 1.0, p1, qI,
                                          (vec3_t){5.0f,0,0}, (vec3_t){0,0,0}));

    vec3_t out_p;
    quat_t out_q;

    /* t = (1.5-0.0)/1.0 = 1.5, extrap = clamp(0.5, 0, 0.5) = 0.5
     * result = 1.0 + 5.0 * (0.5 * 1.0) = 3.5  (not 1.5 as implied would give) */
    ASSERT_TRUE(fr_pose_interpolator_sample(&interp, 1.5, 1e-6f, &out_p, &out_q));
    ASSERT_FLOAT_NEAR(3.5f, out_p.x, 1e-5f);

    return 0;
}

int main(void) {
    if (test_pose_interpolator_samples_midpoint() != 0) {
        return 1;
    }
    if (test_pose_interpolator_clamps_outside_window() != 0) {
        return 1;
    }
    if (test_pose_interpolator_rejects_invalid_args() != 0) {
        return 1;
    }
    if (test_pose_interpolator_server_vel_extrapolation() != 0) {
        return 1;
    }

    printf("p008 pose interpolator tests: PASS\n");
    return 0;
}
