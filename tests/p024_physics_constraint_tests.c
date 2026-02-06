/**
 * @file p024_physics_constraint_tests.c
 * @brief Unit tests for constraint and Jacobian structures (phys-010).
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "ferrum/physics/constraint.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/constants.h"

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

/* ── Helper: create a standard test body pair ───────────────────── */

static void setup_test_bodies(phys_body_t *a, phys_body_t *b) {
    phys_body_init(a);
    phys_body_init(b);
    phys_body_set_mass(a, 1.0f);
    phys_body_set_mass(b, 1.0f);
    phys_body_set_sphere_inertia(a, 1.0f, 0.5f);
    phys_body_set_sphere_inertia(b, 1.0f, 0.5f);
    a->position = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    b->position = (phys_vec3_t){ 0.5f, 0.0f, 0.0f};
}

static phys_contact_point_t make_test_contact(void) {
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));
    contact.point_world = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    contact.normal      = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    contact.penetration = 0.05f;
    return contact;
}

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_jacobian_row_size(void) {
    /* phys_jacobian_row_t: 4 * vec3(12) + 5 * float(4) = 48+20 = 68 bytes */
    ASSERT_TRUE(sizeof(phys_jacobian_row_t) >= 68);
    ASSERT_TRUE(sizeof(phys_jacobian_row_t) <= 80);
    return 0;
}

static int test_constraint_size(void) {
    /* phys_constraint_t: 3*uint32(12) + 2*uint8(2) + 2*pad(2) + 3*row(68+) */
    ASSERT_TRUE(sizeof(phys_constraint_t) >= 200);
    ASSERT_TRUE(sizeof(phys_constraint_t) <= 280);
    return 0;
}

static int test_tangent_basis_y_normal(void) {
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};
    phys_vec3_t t1, t2;
    phys_compute_tangent_basis(normal, &t1, &t2);

    /* Both tangents must be unit length. */
    ASSERT_FLOAT_NEAR(1.0f, vec3_magnitude(t1), 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, vec3_magnitude(t2), 0.001f);

    /* All three vectors must be mutually orthogonal. */
    ASSERT_FLOAT_NEAR(0.0f, vec3_dot(normal, t1), 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, vec3_dot(normal, t2), 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, vec3_dot(t1, t2), 0.001f);
    return 0;
}

static int test_tangent_basis_x_normal(void) {
    phys_vec3_t normal = {1.0f, 0.0f, 0.0f};
    phys_vec3_t t1, t2;
    phys_compute_tangent_basis(normal, &t1, &t2);

    ASSERT_FLOAT_NEAR(1.0f, vec3_magnitude(t1), 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, vec3_magnitude(t2), 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, vec3_dot(normal, t1), 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, vec3_dot(normal, t2), 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, vec3_dot(t1, t2), 0.001f);
    return 0;
}

static int test_tangent_basis_diagonal(void) {
    float inv_sqrt3 = 1.0f / sqrtf(3.0f);
    phys_vec3_t normal = {inv_sqrt3, inv_sqrt3, inv_sqrt3};
    phys_vec3_t t1, t2;
    phys_compute_tangent_basis(normal, &t1, &t2);

    ASSERT_FLOAT_NEAR(1.0f, vec3_magnitude(t1), 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, vec3_magnitude(t2), 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, vec3_dot(normal, t1), 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, vec3_dot(normal, t2), 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, vec3_dot(t1, t2), 0.001f);
    return 0;
}

static int test_tangent_basis_null_safe(void) {
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};
    phys_vec3_t t1;

    /* Should not crash with NULL pointers. */
    phys_compute_tangent_basis(normal, NULL, NULL);
    phys_compute_tangent_basis(normal, &t1, NULL);
    phys_compute_tangent_basis(normal, NULL, &t1);
    return 0;
}

static int test_contact_constraint_row_count(void) {
    phys_body_t a, b;
    setup_test_bodies(&a, &b);
    phys_contact_point_t contact = make_test_contact();

    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    phys_constraint_build_contact(&c, &a, &b, &contact,
                                  0.5f, 0.3f, 1.0f / 60.0f, 0.2f, 0.01f);

    ASSERT_INT_EQ(3, (int)c.row_count);
    return 0;
}

static int test_contact_constraint_normal_row(void) {
    phys_body_t a, b;
    setup_test_bodies(&a, &b);
    phys_contact_point_t contact = make_test_contact();

    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    phys_constraint_build_contact(&c, &a, &b, &contact,
                                  0.5f, 0.3f, 1.0f / 60.0f, 0.2f, 0.01f);

    /* Normal row (row 0): J_va = -normal, J_vb = +normal. */
    ASSERT_VEC3_NEAR(((phys_vec3_t){-1.0f, 0.0f, 0.0f}), c.rows[0].J_va, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 1.0f, 0.0f, 0.0f}), c.rows[0].J_vb, 0.001f);

    /* Normal row: push only (lambda_min = 0). */
    ASSERT_FLOAT_NEAR(0.0f, c.rows[0].lambda_min, 1e-6f);

    /* lambda_max should be large positive. */
    ASSERT_TRUE(c.rows[0].lambda_max > 1e9f);
    return 0;
}

static int test_contact_constraint_angular_jacobian(void) {
    phys_body_t a, b;
    setup_test_bodies(&a, &b);
    phys_contact_point_t contact = make_test_contact();

    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    phys_constraint_build_contact(&c, &a, &b, &contact,
                                  0.5f, 0.3f, 1.0f / 60.0f, 0.2f, 0.01f);

    /* rA = contact.point_world - a.position = (0,0,0) - (-0.5,0,0) = (0.5, 0, 0)
     * rB = contact.point_world - b.position = (0,0,0) - (0.5,0,0)  = (-0.5, 0, 0)
     * n  = (1, 0, 0)
     *
     * J_wa = -(rA × n) = -((0.5,0,0) × (1,0,0)) = -(0,0,0) = (0,0,0)
     * J_wb = +(rB × n) = ((-0.5,0,0) × (1,0,0))  = (0,0,0)
     */
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, 0.0f, 0.0f}), c.rows[0].J_wa, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, 0.0f, 0.0f}), c.rows[0].J_wb, 0.001f);
    return 0;
}

static int test_contact_constraint_friction_rows(void) {
    phys_body_t a, b;
    setup_test_bodies(&a, &b);
    phys_contact_point_t contact = make_test_contact();

    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    phys_constraint_build_contact(&c, &a, &b, &contact,
                                  0.5f, 0.3f, 1.0f / 60.0f, 0.2f, 0.01f);

    /* Friction rows (rows 1, 2): bias should be 0. */
    ASSERT_FLOAT_NEAR(0.0f, c.rows[1].bias, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, c.rows[2].bias, 1e-6f);

    /* Friction rows: lambda_min should be negative, lambda_max positive. */
    ASSERT_TRUE(c.rows[1].lambda_min < 0.0f);
    ASSERT_TRUE(c.rows[1].lambda_max > 0.0f);
    ASSERT_TRUE(c.rows[2].lambda_min < 0.0f);
    ASSERT_TRUE(c.rows[2].lambda_max > 0.0f);
    return 0;
}

static int test_effective_mass_positive(void) {
    phys_body_t a, b;
    setup_test_bodies(&a, &b);
    phys_contact_point_t contact = make_test_contact();

    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    phys_constraint_build_contact(&c, &a, &b, &contact,
                                  0.5f, 0.3f, 1.0f / 60.0f, 0.2f, 0.01f);

    /* Effective mass should be positive for two unit-mass bodies. */
    float eff = phys_compute_effective_mass(&c.rows[0],
                                            a.inv_mass, &a.inv_inertia_diag,
                                            b.inv_mass, &b.inv_inertia_diag);
    ASSERT_TRUE(eff > 0.0f);
    /* Should be less than either body mass (1.0). */
    ASSERT_TRUE(eff < 1.0f);
    return 0;
}

static int test_effective_mass_static_body(void) {
    phys_body_t a, b;
    setup_test_bodies(&a, &b);
    /* Make body A static (inv_mass = 0, inv_inertia = 0). */
    phys_body_set_mass(&a, 0.0f);
    a.inv_inertia_diag = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    phys_contact_point_t contact = make_test_contact();

    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    phys_constraint_build_contact(&c, &a, &b, &contact,
                                  0.5f, 0.3f, 1.0f / 60.0f, 0.2f, 0.01f);

    /* Effective mass should still be positive (based on body B only). */
    float eff = phys_compute_effective_mass(&c.rows[0],
                                            a.inv_mass, &a.inv_inertia_diag,
                                            b.inv_mass, &b.inv_inertia_diag);
    ASSERT_TRUE(eff > 0.0f);
    return 0;
}

static int test_baumgarte_bias_positive(void) {
    phys_body_t a, b;
    setup_test_bodies(&a, &b);
    phys_contact_point_t contact = make_test_contact();
    /* penetration = 0.05 > slop = 0.01, so Baumgarte bias component > 0. */

    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    phys_constraint_build_contact(&c, &a, &b, &contact,
                                  0.5f, 0.3f, 1.0f / 60.0f, 0.2f, 0.01f);

    /* Bias should be positive (pushing bodies apart). */
    ASSERT_TRUE(c.rows[0].bias > 0.0f);
    return 0;
}

static int test_baumgarte_bias_zero_no_penetration(void) {
    phys_body_t a, b;
    setup_test_bodies(&a, &b);
    phys_contact_point_t contact = make_test_contact();
    /* Set penetration below slop — no Baumgarte correction needed. */
    contact.penetration = 0.005f;  /* < slop of 0.01 */

    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    /* restitution=0 and velocities=0, so bias should be 0. */
    phys_constraint_build_contact(&c, &a, &b, &contact,
                                  0.5f, 0.0f, 1.0f / 60.0f, 0.2f, 0.01f);

    /* With zero penetration correction and zero restitution, bias = 0. */
    ASSERT_FLOAT_NEAR(0.0f, c.rows[0].bias, 1e-6f);
    return 0;
}

static int test_constraint_null_safe(void) {
    phys_body_t a, b;
    setup_test_bodies(&a, &b);
    phys_contact_point_t contact = make_test_contact();

    /* All NULL combinations should not crash. */
    phys_constraint_build_contact(NULL, &a, &b, &contact,
                                  0.5f, 0.3f, 1.0f / 60.0f, 0.2f, 0.01f);
    phys_constraint_build_contact(NULL, NULL, NULL, NULL,
                                  0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    phys_constraint_build_contact(&c, NULL, &b, &contact,
                                  0.5f, 0.3f, 1.0f / 60.0f, 0.2f, 0.01f);
    phys_constraint_build_contact(&c, &a, NULL, &contact,
                                  0.5f, 0.3f, 1.0f / 60.0f, 0.2f, 0.01f);
    phys_constraint_build_contact(&c, &a, &b, NULL,
                                  0.5f, 0.3f, 1.0f / 60.0f, 0.2f, 0.01f);

    /* Effective mass with NULL should return 0. */
    float eff = phys_compute_effective_mass(NULL, 1.0f, NULL, 1.0f, NULL);
    ASSERT_FLOAT_NEAR(0.0f, eff, 1e-6f);

    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"jacobian_row_size",                    test_jacobian_row_size},
    {"constraint_size",                      test_constraint_size},
    {"tangent_basis_y_normal",               test_tangent_basis_y_normal},
    {"tangent_basis_x_normal",               test_tangent_basis_x_normal},
    {"tangent_basis_diagonal",               test_tangent_basis_diagonal},
    {"tangent_basis_null_safe",              test_tangent_basis_null_safe},
    {"contact_constraint_row_count",         test_contact_constraint_row_count},
    {"contact_constraint_normal_row",        test_contact_constraint_normal_row},
    {"contact_constraint_angular_jacobian",  test_contact_constraint_angular_jacobian},
    {"contact_constraint_friction_rows",     test_contact_constraint_friction_rows},
    {"effective_mass_positive",              test_effective_mass_positive},
    {"effective_mass_static_body",           test_effective_mass_static_body},
    {"baumgarte_bias_positive",              test_baumgarte_bias_positive},
    {"baumgarte_bias_zero_no_penetration",   test_baumgarte_bias_zero_no_penetration},
    {"constraint_null_safe",                 test_constraint_null_safe},
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
