/**
 * @file p056_physics_box_box_tests.c
 * @brief Unit tests for Box-Box narrowphase (SAT).
 *
 * Tests cover: face contact, separated, edge contact, resting,
 * corner, identical overlap, rotated 45°, and NULL safety.
 */

#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/collision/box_box.h"
#include "ferrum/physics/manifold.h"

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
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: "                    \
                    "expected %d got %d\n",                                     \
                    __FILE__, __LINE__, (int)(exp), (int)(act));                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act), _t = (tol);                              \
        if (fabsf(_e - _a) > _t) {                                             \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f (tol %.6f)\n",                      \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)_t);    \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_VEC3_NEAR(exp, act, tol)                                        \
    do {                                                                        \
        phys_vec3_t _ev = (exp), _av = (act);                                  \
        float _t = (tol);                                                      \
        if (fabsf(_ev.x - _av.x) > _t || fabsf(_ev.y - _av.y) > _t           \
            || fabsf(_ev.z - _av.z) > _t) {                                    \
            fprintf(stderr, "ASSERT_VEC3_NEAR failed: %s:%d: "                \
                    "expected (%.4f,%.4f,%.4f) got (%.4f,%.4f,%.4f)\n",         \
                    __FILE__, __LINE__,                                         \
                    (double)_ev.x, (double)_ev.y, (double)_ev.z,               \
                    (double)_av.x, (double)_av.y, (double)_av.z);              \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-50s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static const phys_quat_t QUAT_IDENTITY = {0.0f, 0.0f, 0.0f, 1.0f};
static const float TOL = 0.01f;

/** Build a quaternion for rotation around Y axis by the given angle. */
static phys_quat_t quat_y(float radians)
{
    float half = radians * 0.5f;
    return (phys_quat_t){0.0f, sinf(half), 0.0f, cosf(half)};
}


/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Two unit boxes overlapping along X axis.
 * Box A at origin, box B at (1.5, 0, 0). Half-extents = (1,1,1).
 * Overlap = 2.0 - 1.5 = 0.5 along X.
 */
static int test_box_box_face_contact(void)
{
    phys_vec3_t ha = {1.0f, 1.0f, 1.0f};
    phys_vec3_t hb = {1.0f, 1.0f, 1.0f};
    phys_vec3_t ca = {0.0f, 0.0f, 0.0f};
    phys_vec3_t cb = {1.5f, 0.0f, 0.0f};

    phys_contact_point_t contacts[4];
    int n = phys_box_vs_box(ca, QUAT_IDENTITY, ha,
                            cb, QUAT_IDENTITY, hb,
                            contacts, 4);
    ASSERT_TRUE(n >= 1);

    /* Normal should point from A to B, approximately along +X. */
    ASSERT_FLOAT_NEAR(1.0f, contacts[0].normal.x, TOL);
    ASSERT_FLOAT_NEAR(0.0f, contacts[0].normal.y, TOL);
    ASSERT_FLOAT_NEAR(0.0f, contacts[0].normal.z, TOL);

    /* Penetration should be ~0.5. */
    ASSERT_FLOAT_NEAR(0.5f, contacts[0].penetration, TOL);

    return 0;
}

/**
 * Two boxes far apart — no contact expected.
 */
static int test_box_box_separated(void)
{
    phys_vec3_t ha = {1.0f, 1.0f, 1.0f};
    phys_vec3_t hb = {1.0f, 1.0f, 1.0f};
    phys_vec3_t ca = {0.0f, 0.0f, 0.0f};
    phys_vec3_t cb = {5.0f, 0.0f, 0.0f};

    phys_contact_point_t contacts[4];
    int n = phys_box_vs_box(ca, QUAT_IDENTITY, ha,
                            cb, QUAT_IDENTITY, hb,
                            contacts, 4);
    ASSERT_INT_EQ(0, n);
    return 0;
}

/**
 * Two boxes rotated so their edges meet.
 * Box A axis-aligned at origin, box B rotated 45° around Y at offset.
 * This should produce an edge-edge contact (single contact point).
 */
static int test_box_box_edge_contact(void)
{
    phys_vec3_t ha = {1.0f, 1.0f, 1.0f};
    phys_vec3_t hb = {1.0f, 1.0f, 1.0f};
    phys_vec3_t ca = {0.0f, 0.0f, 0.0f};
    /* Place rotated box so its edge barely penetrates box A. */
    float diag = sqrtf(2.0f); /* ~1.414 - rotated half-extent in X */
    phys_vec3_t cb = {1.0f + diag - 0.1f, 0.0f, 0.0f};
    phys_quat_t rot_b = quat_y((float)M_PI / 4.0f);

    phys_contact_point_t contacts[4];
    int n = phys_box_vs_box(ca, QUAT_IDENTITY, ha,
                            cb, rot_b, hb,
                            contacts, 4);
    ASSERT_TRUE(n >= 1);

    /* Penetration should be small positive. */
    ASSERT_TRUE(contacts[0].penetration > 0.0f);
    ASSERT_TRUE(contacts[0].penetration < 0.5f);

    return 0;
}

/**
 * Box resting on top of another box.
 * Box A at origin (half-extents 1,1,1), box B at (0,1.8,0).
 * Overlap = 2.0 - 1.8 = 0.2 along Y.
 * Normal should point upward (+Y direction from A to B).
 */
static int test_box_box_resting(void)
{
    phys_vec3_t ha = {1.0f, 1.0f, 1.0f};
    phys_vec3_t hb = {1.0f, 1.0f, 1.0f};
    phys_vec3_t ca = {0.0f, 0.0f, 0.0f};
    phys_vec3_t cb = {0.0f, 1.8f, 0.0f};

    phys_contact_point_t contacts[4];
    int n = phys_box_vs_box(ca, QUAT_IDENTITY, ha,
                            cb, QUAT_IDENTITY, hb,
                            contacts, 4);
    ASSERT_TRUE(n >= 1);

    /* Normal should be approximately (0, 1, 0). */
    ASSERT_FLOAT_NEAR(0.0f, contacts[0].normal.x, TOL);
    ASSERT_FLOAT_NEAR(1.0f, contacts[0].normal.y, TOL);
    ASSERT_FLOAT_NEAR(0.0f, contacts[0].normal.z, TOL);

    /* Penetration ~0.2. */
    ASSERT_FLOAT_NEAR(0.2f, contacts[0].penetration, TOL);

    return 0;
}

/**
 * One box corner poking into the face of another.
 * Box A at origin, box B small and positioned so one corner dips in.
 */
static int test_box_box_corner(void)
{
    phys_vec3_t ha = {2.0f, 2.0f, 2.0f};
    phys_vec3_t hb = {0.5f, 0.5f, 0.5f};
    phys_vec3_t ca = {0.0f, 0.0f, 0.0f};
    /* Small box sitting near the top face of the large box. */
    phys_vec3_t cb = {0.0f, 2.3f, 0.0f};

    phys_contact_point_t contacts[4];
    int n = phys_box_vs_box(ca, QUAT_IDENTITY, ha,
                            cb, QUAT_IDENTITY, hb,
                            contacts, 4);
    ASSERT_TRUE(n >= 1);

    /* Normal should point upward. */
    ASSERT_FLOAT_NEAR(0.0f, contacts[0].normal.x, TOL);
    ASSERT_FLOAT_NEAR(1.0f, contacts[0].normal.y, TOL);
    ASSERT_FLOAT_NEAR(0.0f, contacts[0].normal.z, TOL);

    /* Penetration = 2.0 + 0.5 - 2.3 = 0.2. */
    ASSERT_FLOAT_NEAR(0.2f, contacts[0].penetration, TOL);

    return 0;
}

/**
 * Two identical boxes at the same position — degenerate overlap.
 * Should still produce contacts (not crash or return 0).
 */
static int test_box_box_identical_overlap(void)
{
    phys_vec3_t h = {1.0f, 1.0f, 1.0f};
    phys_vec3_t c = {0.0f, 0.0f, 0.0f};

    phys_contact_point_t contacts[4];
    int n = phys_box_vs_box(c, QUAT_IDENTITY, h,
                            c, QUAT_IDENTITY, h,
                            contacts, 4);
    ASSERT_TRUE(n >= 1);

    /* Penetration should be full overlap = 2.0 (twice the half-extent). */
    ASSERT_TRUE(contacts[0].penetration > 0.0f);

    return 0;
}

/**
 * One box rotated 45° around Y, other axis-aligned.
 * Both at origin shifted so they overlap on the face.
 */
static int test_box_box_rotated_45(void)
{
    phys_vec3_t ha = {1.0f, 1.0f, 1.0f};
    phys_vec3_t hb = {1.0f, 1.0f, 1.0f};
    phys_vec3_t ca = {0.0f, 0.0f, 0.0f};
    phys_vec3_t cb = {2.0f, 0.0f, 0.0f};
    phys_quat_t rot_b = quat_y((float)M_PI / 4.0f);

    phys_contact_point_t contacts[4];
    int n = phys_box_vs_box(ca, QUAT_IDENTITY, ha,
                            cb, rot_b, hb,
                            contacts, 4);
    /* The rotated box's effective extent along X is sqrt(2) ≈ 1.414.
     * Total = 1.0 + 1.414 = 2.414 > 2.0 → overlap.
     */
    ASSERT_TRUE(n >= 1);
    ASSERT_TRUE(contacts[0].penetration > 0.0f);

    return 0;
}

/**
 * NULL contact_out with max_contacts > 0, and max_contacts = 0.
 * Should return 0 safely in both cases.
 */
static int test_box_box_null_safe(void)
{
    phys_vec3_t h = {1.0f, 1.0f, 1.0f};
    phys_vec3_t c = {0.0f, 0.0f, 0.0f};

    /* NULL output with max > 0 → safe, return 0. */
    int n = phys_box_vs_box(c, QUAT_IDENTITY, h,
                            c, QUAT_IDENTITY, h,
                            NULL, 4);
    ASSERT_INT_EQ(0, n);

    /* max_contacts = 0 → return 0. */
    phys_contact_point_t contacts[4];
    n = phys_box_vs_box(c, QUAT_IDENTITY, h,
                        c, QUAT_IDENTITY, h,
                        contacts, 0);
    ASSERT_INT_EQ(0, n);

    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p056_physics_box_box_tests\n");

    RUN_TEST(test_box_box_face_contact);
    RUN_TEST(test_box_box_separated);
    RUN_TEST(test_box_box_edge_contact);
    RUN_TEST(test_box_box_resting);
    RUN_TEST(test_box_box_corner);
    RUN_TEST(test_box_box_identical_overlap);
    RUN_TEST(test_box_box_rotated_45);
    RUN_TEST(test_box_box_null_safe);

    printf("\n%d/%d tests passed.\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
