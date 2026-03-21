/**
 * @file mesh_bone_collision_tests.c
 * @brief Tests for per-bone collision generation and skeleton override (Phase D).
 *
 * Validates that per-bone mesh segments are correctly decomposed into
 * convex hulls and that the resulting collider descriptors can override
 * a skeleton definition's collision data.
 */

#include "ferrum/editor/mesh/mesh_bone_collision.h"
#include "ferrum/editor/mesh/mesh_bone_segment.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/bone_collider.h"
#include "ferrum/physics/convex_decompose.h"

#define _USE_MATH_DEFINES
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Helpers ---- */

/**
 * @brief Build mesh segments for 2 bones, each with enough triangles
 *        for decomposition (a small cube per bone, 12 tris each).
 */
static void build_test_segments(mesh_bone_segments_t *segs) {
    mesh_bone_segments_init(segs, 4);

    /* We need to build phys_triangle_t arrays manually. */
    /* Bone 0: unit cube at origin. */
    phys_triangle_t *tris0 = (phys_triangle_t *)malloc(12 * sizeof(phys_triangle_t));
    /* Bone 1: unit cube at (3,0,0). */
    phys_triangle_t *tris1 = (phys_triangle_t *)malloc(12 * sizeof(phys_triangle_t));

    /* Simple cube face definitions. */
    static const float cube_verts[][3] = {
        {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f},
        { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f},
        {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f},
        { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
    };
    static const uint32_t cube_tris[][3] = {
        {0,1,2},{0,2,3}, {4,6,5},{4,7,6},
        {0,4,5},{0,5,1}, {2,6,7},{2,7,3},
        {0,3,7},{0,7,4}, {1,5,6},{1,6,2},
    };

    for (int t = 0; t < 12; t++) {
        for (int v = 0; v < 3; v++) {
            uint32_t vi = cube_tris[t][v];
            tris0[t].v[v].x = cube_verts[vi][0];
            tris0[t].v[v].y = cube_verts[vi][1];
            tris0[t].v[v].z = cube_verts[vi][2];

            tris1[t].v[v].x = cube_verts[vi][0] + 3.0f;
            tris1[t].v[v].y = cube_verts[vi][1];
            tris1[t].v[v].z = cube_verts[vi][2];
        }
    }

    segs->segments[0].triangles = tris0;
    segs->segments[0].tri_count = 12;
    segs->segments[0].bone_index = 0;
    segs->segments[1].triangles = tris1;
    segs->segments[1].tri_count = 12;
    segs->segments[1].bone_index = 1;
    segs->count = 2;
}

/* ---- Tests: mesh_bone_collision_build ---- */

/** Build collision set from 2-bone segments. */
static void test_build_two_bones(void) {
    mesh_bone_segments_t segs;
    build_test_segments(&segs);

    mesh_bone_collision_set_t set;
    bool ok = mesh_bone_collision_build(&set, &segs);
    ASSERT(ok);
    ASSERT(set.count == 2);

    /* Both should be valid (each cube has 12 tris ≥ 4). */
    if (set.count >= 2) {
        ASSERT(set.entries[0].valid);
        ASSERT(set.entries[1].valid);
        ASSERT(set.entries[0].bone_index == 0);
        ASSERT(set.entries[1].bone_index == 1);

        /* Each should produce at least 1 hull. */
        ASSERT(set.entries[0].decomp.hull_count > 0);
        ASSERT(set.entries[1].decomp.hull_count > 0);
    }

    mesh_bone_collision_destroy(&set);
    mesh_bone_segments_destroy(&segs);
}

/** Build with NULL inputs returns false. */
static void test_build_null(void) {
    mesh_bone_collision_set_t set;
    memset(&set, 0, sizeof(set));
    ASSERT(!mesh_bone_collision_build(NULL, NULL));
    ASSERT(!mesh_bone_collision_build(&set, NULL));
}

/** Destroy is safe on zero-initialized set. */
static void test_destroy_empty(void) {
    mesh_bone_collision_set_t set;
    memset(&set, 0, sizeof(set));
    mesh_bone_collision_destroy(&set); /* Should not crash. */
    g_pass++;
}

/* ---- Tests: mesh_bone_collision_to_collider_descs ---- */

/** Convert collision set to collider descriptors. */
static void test_to_collider_descs(void) {
    mesh_bone_segments_t segs;
    build_test_segments(&segs);

    mesh_bone_collision_set_t set;
    mesh_bone_collision_build(&set, &segs);

    /* Allocate output buffers. */
    bone_collider_desc_t descs[2];
    memset(descs, 0, sizeof(descs));
    float hull_verts[4096];
    uint32_t vert_count = 0;

    bool ok = mesh_bone_collision_to_collider_descs(
        &set, descs, hull_verts, &vert_count, 4096);
    ASSERT(ok);
    ASSERT(vert_count > 0);

    /* Both bones should be CONVEX_HULL type. */
    for (int i = 0; i < 2; i++) {
        ASSERT(descs[i].shape_type == BONE_COLLIDER_CONVEX_HULL);
        ASSERT(descs[i].hull_count > 0);
    }

    /* Hull offsets should not overlap. */
    if (set.count >= 2) {
        uint32_t end0 = descs[0].hull_offset + descs[0].hull_count;
        ASSERT(descs[1].hull_offset >= end0);
    }

    mesh_bone_collision_destroy(&set);
    mesh_bone_segments_destroy(&segs);
}

/** to_collider_descs with insufficient vertex buffer returns false. */
static void test_to_collider_descs_overflow(void) {
    mesh_bone_segments_t segs;
    build_test_segments(&segs);

    mesh_bone_collision_set_t set;
    mesh_bone_collision_build(&set, &segs);

    bone_collider_desc_t descs[2];
    memset(descs, 0, sizeof(descs));
    float hull_verts[3]; /* Intentionally too small. */
    uint32_t vert_count = 0;

    bool ok = mesh_bone_collision_to_collider_descs(
        &set, descs, hull_verts, &vert_count, 3);
    ASSERT(!ok);

    mesh_bone_collision_destroy(&set);
    mesh_bone_segments_destroy(&segs);
}

/* ---- Tests: mesh_bone_collision_override_skeleton ---- */

/** Override skeleton replaces collider data. */
static void test_override_skeleton(void) {
    mesh_bone_segments_t segs;
    build_test_segments(&segs);

    mesh_bone_collision_set_t set;
    mesh_bone_collision_build(&set, &segs);

    /* Create a skeleton copy with 2 joints, no existing colliders. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 0);

    bool ok = mesh_bone_collision_override_skeleton(&skel, &set);
    ASSERT(ok);

    /* Colliders should now be set for both bones. */
    ASSERT(skel.colliders != NULL);
    if (skel.colliders) {
        ASSERT(skel.colliders[0].shape_type == BONE_COLLIDER_CONVEX_HULL);
        ASSERT(skel.colliders[1].shape_type == BONE_COLLIDER_CONVEX_HULL);
        ASSERT(skel.colliders[0].hull_count > 0);
        ASSERT(skel.colliders[1].hull_count > 0);
    }

    /* Hull vertices should be allocated. */
    ASSERT(skel.hull_vertices != NULL);
    ASSERT(skel.hull_vertex_count > 0);

    skeleton_def_destroy(&skel);
    mesh_bone_collision_destroy(&set);
    mesh_bone_segments_destroy(&segs);
}

/** Override skeleton with NULL inputs returns false. */
static void test_override_null(void) {
    ASSERT(!mesh_bone_collision_override_skeleton(NULL, NULL));

    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 0);
    ASSERT(!mesh_bone_collision_override_skeleton(&skel, NULL));
    skeleton_def_destroy(&skel);
}

/** Override skeleton skips bones beyond skeleton's joint count. */
static void test_override_more_bones_than_joints(void) {
    mesh_bone_segments_t segs;
    build_test_segments(&segs); /* 2 bones */

    mesh_bone_collision_set_t set;
    mesh_bone_collision_build(&set, &segs);

    /* Skeleton with only 1 joint. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 1, 0);

    bool ok = mesh_bone_collision_override_skeleton(&skel, &set);
    ASSERT(ok);

    /* Only bone 0 should be set. */
    ASSERT(skel.colliders != NULL);
    if (skel.colliders) {
        ASSERT(skel.colliders[0].shape_type == BONE_COLLIDER_CONVEX_HULL);
    }

    skeleton_def_destroy(&skel);
    mesh_bone_collision_destroy(&set);
    mesh_bone_segments_destroy(&segs);
}

/* ---- Main ---- */

int main(void) {
    printf("mesh_bone_collision_tests:\n");

    test_build_two_bones();
    test_build_null();
    test_destroy_empty();
    test_to_collider_descs();
    test_to_collider_descs_overflow();
    test_override_skeleton();
    test_override_null();
    test_override_more_bones_than_joints();

    printf("mesh_bone_collision_tests: %d passed, %d failed\n",
           g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
