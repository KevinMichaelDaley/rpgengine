#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/math/constants.h"
#include "ferrum/physics/body.h"

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

static int test_struct_size(void) {
    ASSERT_INT_EQ(88, (int)sizeof(phys_body_t));
    return 0;
}

static int test_body_init_defaults(void) {
    phys_body_t b;
    phys_body_init(&b);

    ASSERT_FLOAT_NEAR(0.0f, b.inv_mass, 0.0f);
    ASSERT_TRUE(phys_body_is_static(&b));
    ASSERT_TRUE(!phys_body_is_sleeping(&b));
    ASSERT_TRUE(!phys_body_is_kinematic(&b));
    return 0;
}

static int test_set_mass_computes_inv_mass(void) {
    phys_body_t b;
    phys_body_init(&b);
    phys_body_set_mass(&b, 2.0f);
    ASSERT_FLOAT_NEAR(0.5f, b.inv_mass, 1e-6f);
    ASSERT_TRUE(!phys_body_is_static(&b));
    return 0;
}

static int test_sphere_inertia_diag(void) {
    phys_body_t b;
    phys_body_init(&b);
    phys_body_set_sphere_inertia(&b, 1.0f, 0.5f);

    // I = 2/5 * m * r^2 = 0.1 -> inv = 10
    ASSERT_FLOAT_NEAR(10.0f, b.inv_inertia_diag.x, 0.1f);
    ASSERT_FLOAT_NEAR(10.0f, b.inv_inertia_diag.y, 0.1f);
    ASSERT_FLOAT_NEAR(10.0f, b.inv_inertia_diag.z, 0.1f);
    return 0;
}

static int test_box_inertia_diag(void) {
    phys_body_t b;
    phys_body_init(&b);

    // half extents (hx,hy,hz) = (1,2,3) => full (2,4,6)
    // Ixx = 1/12 m ( (2hy)^2 + (2hz)^2 ) = 1/12*(16+36)=4.333333
    // inv = 0.230769...
    phys_body_set_box_inertia(&b, 1.0f, (phys_vec3_t){1.0f, 2.0f, 3.0f});
    ASSERT_FLOAT_NEAR(0.230769f, b.inv_inertia_diag.x, 0.01f);

    // Iyy = 1/12 m ( (2hx)^2 + (2hz)^2 ) = 1/12*(4+36)=3.333333 -> inv 0.3
    ASSERT_FLOAT_NEAR(0.3f, b.inv_inertia_diag.y, 0.01f);

    // Izz = 1/12 m ( (2hx)^2 + (2hy)^2 ) = 1/12*(4+16)=1.666666 -> inv 0.6
    ASSERT_FLOAT_NEAR(0.6f, b.inv_inertia_diag.z, 0.01f);
    return 0;
}

static int test_capsule_inertia_diag_full_axes(void) {
    phys_body_t b;
    phys_body_init(&b);

    const float m = 1.0f;
    const float r = 0.5f;
    const float half_h = 1.0f;
    phys_body_set_capsule_inertia(&b, m, r, half_h);

    // Reference: capsule aligned with +Y axis, modeled as cylinder (L=2*half_h)
    // plus two solid hemispheres. Mass split by volume.
    const float L = 2.0f * half_h;
    const float Vc = FERRUM_PI * r * r * L;
    const float Vs = (4.0f / 3.0f) * FERRUM_PI * r * r * r;
    const float mtot = Vc + Vs;
    const float mc = m * (Vc / mtot);
    const float ms = m * (Vs / mtot);
    const float mh = 0.5f * ms;

    const float Iyy_c = 0.5f * mc * r * r;
    const float Ixx_c = (1.0f / 12.0f) * mc * (3.0f * r * r + L * L);

    const float d_com_from_flat = (3.0f / 8.0f) * r;
    const float Iyy_h_com = (2.0f / 5.0f) * mh * r * r;
    const float Ixx_h_center = (2.0f / 5.0f) * mh * r * r;
    const float Ixx_h_com = Ixx_h_center - mh * d_com_from_flat * d_com_from_flat;

    const float y_off = half_h + d_com_from_flat;
    const float Ixx_h_origin = Ixx_h_com + mh * y_off * y_off;

    const float Ixx = Ixx_c + 2.0f * Ixx_h_origin;
    const float Iyy = Iyy_c + 2.0f * Iyy_h_com;
    const float Izz = Ixx;

    ASSERT_TRUE(Ixx > 0.0f && Iyy > 0.0f && Izz > 0.0f);
    ASSERT_FLOAT_NEAR(1.0f / Ixx, b.inv_inertia_diag.x, 1e-3f);
    ASSERT_FLOAT_NEAR(1.0f / Iyy, b.inv_inertia_diag.y, 1e-3f);
    ASSERT_FLOAT_NEAR(1.0f / Izz, b.inv_inertia_diag.z, 1e-3f);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"struct_size", test_struct_size},
    {"body_init_defaults", test_body_init_defaults},
    {"set_mass_computes_inv_mass", test_set_mass_computes_inv_mass},
    {"sphere_inertia_diag", test_sphere_inertia_diag},
    {"box_inertia_diag", test_box_inertia_diag},
    {"capsule_inertia_diag_full_axes", test_capsule_inertia_diag_full_axes},
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
