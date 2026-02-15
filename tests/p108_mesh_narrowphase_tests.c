/**
 * @file p108_mesh_narrowphase_tests.c
 * @brief Unit tests for Step 9.2: Primitive-vs-Mesh Narrowphase.
 *
 * Tests:
 *   - Sphere vs single triangle (hit, miss, edge, vertex, speculative)
 *   - Box vs single triangle (hit, miss)
 *   - Capsule vs single triangle (hit, miss)
 *   - BVH traversal: sphere vs multi-triangle mesh
 *   - BVH traversal: box vs multi-triangle mesh
 *   - BVH traversal: capsule vs multi-triangle mesh
 *   - NULL safety
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/narrowphase.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/mesh_narrowphase.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act), _t = (tol);                              \
        if (fabsf(_e - _a) > _t) {                                             \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f (tol=%.6f)\n",                      \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)_t);   \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-60s", #fn);                                                 \
        int _r = fn();                                                          \
        if (_r) { printf("[FAIL]\n"); fail_count++; }                           \
        else    { printf("[OK]\n"); }                                           \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

#define ARENA_SIZE (4u * 1024u * 1024u)

static phys_triangle_t make_tri(float x0, float y0, float z0,
                                 float x1, float y1, float z1,
                                 float x2, float y2, float z2) {
    phys_triangle_t t;
    t.v[0] = (phys_vec3_t){x0, y0, z0};
    t.v[1] = (phys_vec3_t){x1, y1, z1};
    t.v[2] = (phys_vec3_t){x2, y2, z2};
    return t;
}

/* A flat triangle on Y=0 plane: (0,0,0)-(10,0,0)-(0,0,10). */
static phys_triangle_t floor_tri(void) {
    return make_tri(0, 0, 0,  10, 0, 0,  0, 0, 10);
}

/* ── Sphere vs Triangle ────────────────────────────────────────── */

/** Sphere directly above triangle face — should hit. */
static int test_sphere_vs_tri_hit(void) {
    phys_triangle_t tri = floor_tri();
    phys_contact_point_t c;
    memset(&c, 0, sizeof(c));

    /* Sphere at (2, 0.5, 2), radius 1.0 → 0.5 above Y=0 face → penetration 0.5. */
    bool hit = phys_sphere_vs_triangle(
        (phys_vec3_t){2, 0.5f, 2}, 1.0f,
        &tri, 0.0f, &c);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.5f, c.penetration, 0.01f);
    /* Normal should point upward (Y+). */
    ASSERT_FLOAT_NEAR(0.0f, c.normal.x, 0.01f);
    ASSERT_TRUE(c.normal.y > 0.5f);
    ASSERT_FLOAT_NEAR(0.0f, c.normal.z, 0.01f);

    return 0;
}

/** Sphere far above triangle — should miss. */
static int test_sphere_vs_tri_miss(void) {
    phys_triangle_t tri = floor_tri();
    phys_contact_point_t c;
    memset(&c, 0, sizeof(c));

    bool hit = phys_sphere_vs_triangle(
        (phys_vec3_t){2, 5.0f, 2}, 1.0f,
        &tri, 0.0f, &c);

    ASSERT_TRUE(!hit);
    return 0;
}

/** Sphere near triangle edge — should hit. */
static int test_sphere_vs_tri_edge(void) {
    phys_triangle_t tri = floor_tri();
    phys_contact_point_t c;
    memset(&c, 0, sizeof(c));

    /* Sphere just outside the X edge at y=0, close enough to touch. */
    bool hit = phys_sphere_vs_triangle(
        (phys_vec3_t){5, 0.0f, -0.3f}, 0.5f,
        &tri, 0.0f, &c);

    ASSERT_TRUE(hit);
    ASSERT_TRUE(c.penetration > 0.0f);
    return 0;
}

/** Sphere near triangle vertex — should hit. */
static int test_sphere_vs_tri_vertex(void) {
    phys_triangle_t tri = floor_tri();
    phys_contact_point_t c;
    memset(&c, 0, sizeof(c));

    /* Sphere centered near vertex (0,0,0). */
    bool hit = phys_sphere_vs_triangle(
        (phys_vec3_t){-0.2f, 0.0f, -0.2f}, 0.5f,
        &tri, 0.0f, &c);

    ASSERT_TRUE(hit);
    ASSERT_TRUE(c.penetration > 0.0f);
    return 0;
}

/** Speculative contact: sphere separated but within margin. */
static int test_sphere_vs_tri_speculative(void) {
    phys_triangle_t tri = floor_tri();
    phys_contact_point_t c;
    memset(&c, 0, sizeof(c));

    /* Sphere at (2, 1.5, 2), radius 1.0 → 0.5 gap. Margin 1.0 → should hit speculative. */
    bool hit = phys_sphere_vs_triangle(
        (phys_vec3_t){2, 1.5f, 2}, 1.0f,
        &tri, 1.0f, &c);

    ASSERT_TRUE(hit);
    ASSERT_TRUE(c.penetration < 0.0f); /* Negative = speculative */
    return 0;
}

/* ── Box vs Triangle ───────────────────────────────────────────── */

/** Box resting on triangle face — should hit. */
static int test_box_vs_tri_hit(void) {
    phys_triangle_t tri = floor_tri();
    phys_contact_point_t contacts[4];
    memset(contacts, 0, sizeof(contacts));

    /* Box centered at (3, 0.4, 3), half-extents (0.5, 0.5, 0.5), identity rotation.
     * Bottom face at y = -0.1 → overlaps Y=0 plane by 0.1. */
    int nc = phys_box_vs_triangle(
        (phys_vec3_t){3, 0.4f, 3},
        (phys_quat_t){0, 0, 0, 1},
        (phys_vec3_t){0.5f, 0.5f, 0.5f},
        &tri, 0.0f, contacts, 4);

    ASSERT_TRUE(nc > 0);
    ASSERT_TRUE(contacts[0].penetration > 0.0f);
    return 0;
}

/** Box far above triangle — should miss. */
static int test_box_vs_tri_miss(void) {
    phys_triangle_t tri = floor_tri();
    phys_contact_point_t contacts[4];
    memset(contacts, 0, sizeof(contacts));

    int nc = phys_box_vs_triangle(
        (phys_vec3_t){3, 5.0f, 3},
        (phys_quat_t){0, 0, 0, 1},
        (phys_vec3_t){0.5f, 0.5f, 0.5f},
        &tri, 0.0f, contacts, 4);

    ASSERT_TRUE(nc == 0);
    return 0;
}

/* ── Capsule vs Triangle ───────────────────────────────────────── */

/** Capsule lying on triangle face — should hit. */
static int test_capsule_vs_tri_hit(void) {
    phys_triangle_t tri = floor_tri();
    phys_contact_point_t c;
    memset(&c, 0, sizeof(c));

    /* Vertical capsule at (3, 0.4, 3), radius 0.5, half_height 1.0.
     * Bottom sphere at y = 0.4 - 1.0 = -0.6, plus radius means
     * lowest point at y = -0.6 - 0.5 = -1.1. Overlap. */
    /* Actually: capsule segment from (3, -0.6, 3) to (3, 1.4, 3).
     * Closest point on segment to Y=0 plane is (3, -0.6, 3).
     * Distance = 0.6, radius = 0.5 → actually miss with 0.1 gap.
     * Use closer position. */
    bool hit = phys_capsule_vs_triangle(
        (phys_vec3_t){3, 0.3f, 3},
        (phys_quat_t){0, 0, 0, 1},  /* identity = Y-axis aligned */
        0.5f, 1.0f,
        &tri, 0.0f, &c);

    ASSERT_TRUE(hit);
    ASSERT_TRUE(c.penetration > 0.0f);
    return 0;
}

/** Capsule far above triangle — should miss. */
static int test_capsule_vs_tri_miss(void) {
    phys_triangle_t tri = floor_tri();
    phys_contact_point_t c;
    memset(&c, 0, sizeof(c));

    bool hit = phys_capsule_vs_triangle(
        (phys_vec3_t){3, 10.0f, 3},
        (phys_quat_t){0, 0, 0, 1},
        0.5f, 1.0f,
        &tri, 0.0f, &c);

    ASSERT_TRUE(!hit);
    return 0;
}

/* ── BVH traversal: multi-triangle mesh ────────────────────────── */

/** Sphere vs mesh with BVH — find contacts from relevant triangles. */
static int test_sphere_vs_mesh_bvh(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, ARENA_SIZE);

    /* 4 triangles spread out. */
    phys_triangle_t tris[4] = {
        make_tri(0,0,0, 5,0,0, 0,0,5),      /* near origin */
        make_tri(20,0,0, 25,0,0, 20,0,5),    /* far right */
        make_tri(0,0,20, 5,0,20, 0,0,25),    /* far back */
        make_tri(20,0,20, 25,0,20, 20,0,25), /* far corner */
    };
    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, tris, 4, &arena);

    /* Sphere near origin — should only hit triangle 0. */
    phys_contact_point_t contacts[8];
    int nc = phys_sphere_vs_mesh(
        (phys_vec3_t){2, 0.3f, 2}, 0.5f,
        tris, &bvh, 0.0f,
        contacts, 8);

    ASSERT_TRUE(nc >= 1);
    ASSERT_TRUE(contacts[0].penetration > 0.0f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/** Box vs mesh with BVH. */
static int test_box_vs_mesh_bvh(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, ARENA_SIZE);

    phys_triangle_t tris[4] = {
        make_tri(0,0,0, 5,0,0, 0,0,5),
        make_tri(20,0,0, 25,0,0, 20,0,5),
        make_tri(0,0,20, 5,0,20, 0,0,25),
        make_tri(20,0,20, 25,0,20, 20,0,25),
    };
    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, tris, 4, &arena);

    phys_contact_point_t contacts[16];
    int nc = phys_box_vs_mesh(
        (phys_vec3_t){2, 0.4f, 2},
        (phys_quat_t){0, 0, 0, 1},
        (phys_vec3_t){0.5f, 0.5f, 0.5f},
        tris, &bvh, 0.0f,
        contacts, 16);

    ASSERT_TRUE(nc >= 1);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/** Capsule vs mesh with BVH. */
static int test_capsule_vs_mesh_bvh(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, ARENA_SIZE);

    phys_triangle_t tris[4] = {
        make_tri(0,0,0, 5,0,0, 0,0,5),
        make_tri(20,0,0, 25,0,0, 20,0,5),
        make_tri(0,0,20, 5,0,20, 0,0,25),
        make_tri(20,0,20, 25,0,20, 20,0,25),
    };
    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, tris, 4, &arena);

    phys_contact_point_t contacts[8];
    int nc = phys_capsule_vs_mesh(
        (phys_vec3_t){2, 0.3f, 2},
        (phys_quat_t){0, 0, 0, 1},
        0.5f, 1.0f,
        tris, &bvh, 0.0f,
        contacts, 8);

    ASSERT_TRUE(nc >= 1);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/** NULL safety for all mesh narrowphase functions. */
static int test_mesh_narrowphase_null_safe(void) {
    phys_contact_point_t c;
    memset(&c, 0, sizeof(c));

    ASSERT_TRUE(!phys_sphere_vs_triangle(
        (phys_vec3_t){0,0,0}, 1.0f, NULL, 0.0f, &c));
    ASSERT_TRUE(!phys_sphere_vs_triangle(
        (phys_vec3_t){0,0,0}, 1.0f, NULL, 0.0f, NULL));

    ASSERT_TRUE(phys_box_vs_triangle(
        (phys_vec3_t){0,0,0}, (phys_quat_t){0,0,0,1},
        (phys_vec3_t){1,1,1}, NULL, 0.0f, &c, 1) == 0);

    ASSERT_TRUE(!phys_capsule_vs_triangle(
        (phys_vec3_t){0,0,0}, (phys_quat_t){0,0,0,1},
        0.5f, 1.0f, NULL, 0.0f, &c));

    ASSERT_TRUE(phys_sphere_vs_mesh(
        (phys_vec3_t){0,0,0}, 1.0f, NULL, NULL, 0.0f, &c, 1) == 0);

    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    printf("RUN p108_mesh_narrowphase_tests\n");

    int fail_count = 0;
    int test_count = 0;

    RUN_TEST(test_sphere_vs_tri_hit);
    RUN_TEST(test_sphere_vs_tri_miss);
    RUN_TEST(test_sphere_vs_tri_edge);
    RUN_TEST(test_sphere_vs_tri_vertex);
    RUN_TEST(test_sphere_vs_tri_speculative);
    RUN_TEST(test_box_vs_tri_hit);
    RUN_TEST(test_box_vs_tri_miss);
    RUN_TEST(test_capsule_vs_tri_hit);
    RUN_TEST(test_capsule_vs_tri_miss);
    RUN_TEST(test_sphere_vs_mesh_bvh);
    RUN_TEST(test_box_vs_mesh_bvh);
    RUN_TEST(test_capsule_vs_mesh_bvh);
    RUN_TEST(test_mesh_narrowphase_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
