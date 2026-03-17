/**
 * @file mesh_mesh_tests.c
 * @brief Tests for mesh-vs-mesh BVH-accelerated narrowphase.
 */

#include "ferrum/physics/mesh_narrowphase.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/phys_pool.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define ASSERT(expr) do { \
    if (!(expr)) { \
        printf("  FAIL: %s (line %d)\n", #expr, __LINE__); \
        return 0; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps))

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) printf("OK   %s\n", #fn); \
    else { printf("FAIL %s\n", #fn); fails++; } \
    total++; \
} while (0)

/* ---- Helpers ---- */

/** Build a simple 2-triangle quad in XZ at given Y height. */
static void make_quad_tris(phys_triangle_t *tris, float y, float offset_x) {
    tris[0] = (phys_triangle_t){{
        {-0.5f + offset_x, y, -0.5f},
        { 0.5f + offset_x, y, -0.5f},
        { 0.5f + offset_x, y,  0.5f}
    }};
    tris[1] = (phys_triangle_t){{
        {-0.5f + offset_x, y, -0.5f},
        { 0.5f + offset_x, y,  0.5f},
        {-0.5f + offset_x, y,  0.5f}
    }};
}

/* ---- Tests ---- */

/** Two overlapping quads at same Y. */
static int test_mesh_mesh_overlap(void) {
    phys_triangle_t tris_a[2], tris_b[2];
    make_quad_tris(tris_a, 0.0f, 0.0f);
    make_quad_tris(tris_b, 0.0f, 0.25f);  /* Overlapping. */

    /* Build BVHs. */
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 64);

    phys_mesh_bvh_t bvh_a, bvh_b;
    phys_mesh_bvh_build(&bvh_a, tris_a, 2, &arena);
    phys_mesh_bvh_build(&bvh_b, tris_b, 2, &arena);

    phys_contact_point_t contacts[4];
    int n = phys_mesh_vs_mesh(
        tris_a, &bvh_a, tris_b, &bvh_b,
        0.0f, contacts, 4);

    ASSERT(n > 0);

    phys_frame_arena_destroy(&arena);
    return 1;
}

/** Two separated quads at different Y. */
static int test_mesh_mesh_separated(void) {
    phys_triangle_t tris_a[2], tris_b[2];
    make_quad_tris(tris_a, 0.0f, 0.0f);
    make_quad_tris(tris_b, 5.0f, 0.0f);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 64);

    phys_mesh_bvh_t bvh_a, bvh_b;
    phys_mesh_bvh_build(&bvh_a, tris_a, 2, &arena);
    phys_mesh_bvh_build(&bvh_b, tris_b, 2, &arena);

    phys_contact_point_t contacts[4];
    int n = phys_mesh_vs_mesh(
        tris_a, &bvh_a, tris_b, &bvh_b,
        0.0f, contacts, 4);

    ASSERT(n == 0);

    phys_frame_arena_destroy(&arena);
    return 1;
}

/** Two crossing quads (one in XZ, one in XY). */
static int test_mesh_mesh_crossing(void) {
    /* Quad A in XZ plane at Y=0. */
    phys_triangle_t tris_a[2];
    make_quad_tris(tris_a, 0.0f, 0.0f);

    /* Quad B in XY plane at Z=0. */
    phys_triangle_t tris_b[2] = {
        {{{-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}}},
        {{{-0.5f, -0.5f, 0}, {0.5f,  0.5f, 0}, {-0.5f, 0.5f, 0}}}
    };

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 64);

    phys_mesh_bvh_t bvh_a, bvh_b;
    phys_mesh_bvh_build(&bvh_a, tris_a, 2, &arena);
    phys_mesh_bvh_build(&bvh_b, tris_b, 2, &arena);

    phys_contact_point_t contacts[4];
    int n = phys_mesh_vs_mesh(
        tris_a, &bvh_a, tris_b, &bvh_b,
        0.0f, contacts, 4);

    ASSERT(n > 0);
    /* At least one contact should have positive penetration. */
    float max_pen = 0.0f;
    for (int i = 0; i < n; ++i) {
        if (contacts[i].penetration > max_pen) max_pen = contacts[i].penetration;
    }
    ASSERT(max_pen > 0.0f);

    phys_frame_arena_destroy(&arena);
    return 1;
}

/** NULL inputs should not crash. */
static int test_mesh_mesh_null_safety(void) {
    phys_contact_point_t contacts[4];
    ASSERT(phys_mesh_vs_mesh(NULL, NULL, NULL, NULL, 0, contacts, 4) == 0);
    return 1;
}

/* ---- Main ---- */

int main(void) {
    int fails = 0, total = 0;

    RUN(test_mesh_mesh_overlap);
    RUN(test_mesh_mesh_separated);
    RUN(test_mesh_mesh_crossing);
    RUN(test_mesh_mesh_null_safety);

    printf("\n%d / %d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
