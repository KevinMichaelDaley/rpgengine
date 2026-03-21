/**
 * @file bone_pose_store_tests.c
 * @brief Tests for bone_pose_store: per-entity bone pose override storage.
 *
 * Tests cover init/destroy lifecycle, ensure clones skeleton data,
 * independent blocks per entity, get/get_mut queries, remove + recycle,
 * pool-full behavior, and NULL/edge-case safety.
 */

#include "ferrum/editor/scene/bone_pose/bone_pose_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define ASSERT_FLOAT_EQ(a, b) ASSERT(fabsf((a) - (b)) < 1e-6f)

/* ---- Helper: build a minimal skeleton_def_t for testing ---- */

/**
 * Allocate and fill a skeleton_def_t with `n` joints.
 * rest_local[i] and rest_world[i] are set to identity with
 * translation.x = (float)(i+1) for distinguishability.
 * tail_positions[i] = { i*0.1, i*0.2, i*0.3 }.
 */
static bool make_test_skeleton(skeleton_def_t *skel, uint32_t n) {
    if (!skeleton_def_init(skel, n, 0)) return false;
    for (uint32_t i = 0; i < n; i++) {
        skel->rest_local[i] = mat4_identity();
        skel->rest_local[i].m[12] = (float)(i + 1);  /* tx */
        skel->rest_world[i] = mat4_identity();
        skel->rest_world[i].m[12] = (float)(i + 1) * 10.0f;
    }
    /* Allocate tail_positions. */
    skel->tail_positions = calloc(3 * n, sizeof(float));
    if (!skel->tail_positions) return false;
    for (uint32_t i = 0; i < n; i++) {
        skel->tail_positions[i * 3 + 0] = (float)i * 0.1f;
        skel->tail_positions[i * 3 + 1] = (float)i * 0.2f;
        skel->tail_positions[i * 3 + 2] = (float)i * 0.3f;
    }
    return true;
}

/* ---- Tests ---- */

static void test_init_destroy(void) {
    bone_pose_store_t store;
    bool ok = bone_pose_store_init(&store, 1024);
    ASSERT(ok);
    ASSERT(store.entity_slot != NULL);
    ASSERT(store.blocks != NULL);
    ASSERT(store.block_count == 0);
    ASSERT(store.block_cap > 0);
    bone_pose_store_destroy(&store);
}

static void test_init_zero_capacity(void) {
    bone_pose_store_t store;
    bool ok = bone_pose_store_init(&store, 0);
    ASSERT(!ok);
}

static void test_init_null(void) {
    bool ok = bone_pose_store_init(NULL, 1024);
    ASSERT(!ok);
}

static void test_ensure_clones_skeleton(void) {
    skeleton_def_t skel;
    ASSERT(make_test_skeleton(&skel, 4));

    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    bone_pose_block_t *block = bone_pose_store_ensure(&store, 7, &skel);
    ASSERT(block != NULL);
    ASSERT(block->entity_id == 7);
    ASSERT(block->bone_count == 4);
    ASSERT(block->active);

    /* Verify rest_local was cloned (not shared pointer). */
    ASSERT(block->rest_local != skel.rest_local);
    for (uint32_t i = 0; i < 4; i++) {
        ASSERT_FLOAT_EQ(block->rest_local[i].m[12], (float)(i + 1));
    }

    /* Verify rest_world was cloned. */
    ASSERT(block->rest_world != skel.rest_world);
    for (uint32_t i = 0; i < 4; i++) {
        ASSERT_FLOAT_EQ(block->rest_world[i].m[12], (float)(i + 1) * 10.0f);
    }

    /* Verify tail_positions were cloned. */
    ASSERT(block->tail_positions != skel.tail_positions);
    for (uint32_t i = 0; i < 4; i++) {
        ASSERT_FLOAT_EQ(block->tail_positions[i * 3 + 0], (float)i * 0.1f);
        ASSERT_FLOAT_EQ(block->tail_positions[i * 3 + 1], (float)i * 0.2f);
        ASSERT_FLOAT_EQ(block->tail_positions[i * 3 + 2], (float)i * 0.3f);
    }

    bone_pose_store_destroy(&store);
    skeleton_def_destroy(&skel);
}

static void test_ensure_idempotent(void) {
    /* Calling ensure twice for same entity returns the same block. */
    skeleton_def_t skel;
    ASSERT(make_test_skeleton(&skel, 2));

    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    bone_pose_block_t *b1 = bone_pose_store_ensure(&store, 5, &skel);
    bone_pose_block_t *b2 = bone_pose_store_ensure(&store, 5, &skel);
    ASSERT(b1 == b2);
    ASSERT(store.block_count == 1);

    bone_pose_store_destroy(&store);
    skeleton_def_destroy(&skel);
}

static void test_two_entities_independent(void) {
    skeleton_def_t skel;
    ASSERT(make_test_skeleton(&skel, 3));

    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    bone_pose_block_t *ba = bone_pose_store_ensure(&store, 10, &skel);
    bone_pose_block_t *bb = bone_pose_store_ensure(&store, 20, &skel);
    ASSERT(ba != NULL);
    ASSERT(bb != NULL);
    ASSERT(ba != bb);
    ASSERT(store.block_count == 2);

    /* Modify one — should not affect the other. */
    ba->rest_local[0].m[12] = 999.0f;
    ASSERT_FLOAT_EQ(bb->rest_local[0].m[12], 1.0f);

    bone_pose_store_destroy(&store);
    skeleton_def_destroy(&skel);
}

static void test_get_returns_block(void) {
    skeleton_def_t skel;
    ASSERT(make_test_skeleton(&skel, 2));

    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    bone_pose_store_ensure(&store, 42, &skel);
    const bone_pose_block_t *got = bone_pose_store_get(&store, 42);
    ASSERT(got != NULL);
    ASSERT(got->entity_id == 42);

    bone_pose_store_destroy(&store);
    skeleton_def_destroy(&skel);
}

static void test_get_returns_null_absent(void) {
    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    const bone_pose_block_t *got = bone_pose_store_get(&store, 99);
    ASSERT(got == NULL);

    bone_pose_store_destroy(&store);
}

static void test_get_mut_returns_mutable(void) {
    skeleton_def_t skel;
    ASSERT(make_test_skeleton(&skel, 2));

    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    bone_pose_store_ensure(&store, 7, &skel);
    bone_pose_block_t *got = bone_pose_store_get_mut(&store, 7);
    ASSERT(got != NULL);
    /* Can modify through get_mut. */
    got->rest_local[0].m[12] = 123.0f;
    const bone_pose_block_t *cg = bone_pose_store_get(&store, 7);
    ASSERT_FLOAT_EQ(cg->rest_local[0].m[12], 123.0f);

    bone_pose_store_destroy(&store);
    skeleton_def_destroy(&skel);
}

static void test_has(void) {
    skeleton_def_t skel;
    ASSERT(make_test_skeleton(&skel, 2));

    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    ASSERT(!bone_pose_store_has(&store, 5));
    bone_pose_store_ensure(&store, 5, &skel);
    ASSERT(bone_pose_store_has(&store, 5));
    ASSERT(!bone_pose_store_has(&store, 6));

    bone_pose_store_destroy(&store);
    skeleton_def_destroy(&skel);
}

static void test_remove_recycles_block(void) {
    skeleton_def_t skel;
    ASSERT(make_test_skeleton(&skel, 2));

    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    bone_pose_store_ensure(&store, 10, &skel);
    ASSERT(store.block_count == 1);
    ASSERT(bone_pose_store_has(&store, 10));

    bone_pose_store_remove(&store, 10);
    ASSERT(!bone_pose_store_has(&store, 10));
    ASSERT(bone_pose_store_get(&store, 10) == NULL);

    /* Block was recycled — ensure for a new entity should reuse slot. */
    bone_pose_block_t *nb = bone_pose_store_ensure(&store, 20, &skel);
    ASSERT(nb != NULL);
    ASSERT(nb->entity_id == 20);
    /* block_count should still be 1 since we recycled. */
    ASSERT(store.block_count == 1);

    bone_pose_store_destroy(&store);
    skeleton_def_destroy(&skel);
}

static void test_remove_absent_safe(void) {
    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    /* Should not crash. */
    bone_pose_store_remove(&store, 999);
    ASSERT(store.block_count == 0);

    bone_pose_store_destroy(&store);
}

static void test_pool_full_returns_null(void) {
    skeleton_def_t skel;
    ASSERT(make_test_skeleton(&skel, 1));

    /* Create store with very small entity capacity. Use a custom block cap. */
    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    /* Fill the block pool to capacity. */
    uint32_t cap = store.block_cap;
    for (uint32_t i = 0; i < cap; i++) {
        bone_pose_block_t *b = bone_pose_store_ensure(&store, i, &skel);
        ASSERT(b != NULL);
    }

    /* Next ensure should return NULL (pool full). */
    bone_pose_block_t *overflow = bone_pose_store_ensure(&store, cap + 1, &skel);
    ASSERT(overflow == NULL);

    bone_pose_store_destroy(&store);
    skeleton_def_destroy(&skel);
}

static void test_null_store_args_safe(void) {
    skeleton_def_t skel;
    ASSERT(make_test_skeleton(&skel, 1));

    /* All functions should handle NULL store gracefully. */
    ASSERT(bone_pose_store_ensure(NULL, 1, &skel) == NULL);
    ASSERT(bone_pose_store_get(NULL, 1) == NULL);
    ASSERT(bone_pose_store_get_mut(NULL, 1) == NULL);
    ASSERT(!bone_pose_store_has(NULL, 1));
    bone_pose_store_remove(NULL, 1);  /* should not crash */
    bone_pose_store_destroy(NULL);    /* should not crash */

    skeleton_def_destroy(&skel);
}

static void test_null_skeleton_ensure(void) {
    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    bone_pose_block_t *b = bone_pose_store_ensure(&store, 1, NULL);
    ASSERT(b == NULL);

    bone_pose_store_destroy(&store);
}

static void test_zero_bone_skeleton(void) {
    /* skeleton_def_init requires >= 1 joint, so a zero-bone skeleton
     * would have joint_count == 0 — ensure should return NULL. */
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));

    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    bone_pose_block_t *b = bone_pose_store_ensure(&store, 1, &skel);
    ASSERT(b == NULL);

    bone_pose_store_destroy(&store);
}

static void test_entity_id_at_capacity_boundary(void) {
    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 64));

    skeleton_def_t skel;
    ASSERT(make_test_skeleton(&skel, 1));

    /* Entity ID within bounds should work. */
    bone_pose_block_t *b = bone_pose_store_ensure(&store, 63, &skel);
    ASSERT(b != NULL);

    /* Entity ID at exactly capacity should fail (out of bounds). */
    bone_pose_block_t *b2 = bone_pose_store_ensure(&store, 64, &skel);
    ASSERT(b2 == NULL);

    /* Entity ID far beyond capacity should fail. */
    bone_pose_block_t *b3 = bone_pose_store_ensure(&store, 9999, &skel);
    ASSERT(b3 == NULL);

    bone_pose_store_destroy(&store);
    skeleton_def_destroy(&skel);
}

static void test_large_bone_count_clamped(void) {
    /* Skeletons with > BONE_POSE_MAX_BONES should be clamped. */
    skeleton_def_t skel;
    /* Make a skeleton with 300 bones — exceeds the 256 limit. */
    ASSERT(skeleton_def_init(&skel, 300, 0));
    for (uint32_t i = 0; i < 300; i++) {
        skel.rest_local[i] = mat4_identity();
        skel.rest_world[i] = mat4_identity();
    }
    skel.tail_positions = calloc(3 * 300, sizeof(float));

    bone_pose_store_t store;
    ASSERT(bone_pose_store_init(&store, 1024));

    bone_pose_block_t *b = bone_pose_store_ensure(&store, 1, &skel);
    ASSERT(b != NULL);
    ASSERT(b->bone_count <= BONE_POSE_MAX_BONES);

    bone_pose_store_destroy(&store);
    skeleton_def_destroy(&skel);
}

/* ---- Main ---- */

int main(void) {
    test_init_destroy();
    test_init_zero_capacity();
    test_init_null();
    test_ensure_clones_skeleton();
    test_ensure_idempotent();
    test_two_entities_independent();
    test_get_returns_block();
    test_get_returns_null_absent();
    test_get_mut_returns_mutable();
    test_has();
    test_remove_recycles_block();
    test_remove_absent_safe();
    test_pool_full_returns_null();
    test_null_store_args_safe();
    test_null_skeleton_ensure();
    test_zero_bone_skeleton();
    test_entity_id_at_capacity_boundary();
    test_large_bone_count_clamped();

    printf("bone_pose_store_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
