/**
 * @file p107_mesh_collider_bvh_tests.c
 * @brief Unit tests for Step 9.1: Mesh Collider BVH.
 *
 * Tests cover:
 *   - NULL safety
 *   - Empty mesh (zero triangles)
 *   - Single triangle
 *   - Two triangles (verifies internal node + 2 leaves)
 *   - Many triangles with tree invariants (leaf count, bounds containment)
 *   - SAH quality check: tree height bounded by O(log N)
 *   - AABB query against mesh BVH
 *   - AABB query miss (no overlap)
 *   - Degenerate triangles (zero-area)
 *   - Coplanar triangles (all centroids on one plane)
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/phys_pool.h"

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
        int _e = (exp), _a = (act);                                             \
        if (_e != _a) {                                                         \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n",\
                    __FILE__, __LINE__, _e, _a);                                \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                               \
    do {                                                                        \
        uint32_t _e = (exp), _a = (act);                                        \
        if (_e != _a) {                                                         \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %u got %u\n",\
                    __FILE__, __LINE__, _e, _a);                                \
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

#define ARENA_SIZE (4u * 1024u * 1024u) /* 4 MiB */

static int setup_arena(phys_frame_arena_t *arena) {
    return phys_frame_arena_init(arena, ARENA_SIZE);
}

static void teardown_arena(phys_frame_arena_t *arena) {
    phys_frame_arena_destroy(arena);
}

/** Make a simple triangle from three points. */
static phys_triangle_t make_tri(float x0, float y0, float z0,
                                 float x1, float y1, float z1,
                                 float x2, float y2, float z2) {
    phys_triangle_t t;
    t.v[0] = (phys_vec3_t){x0, y0, z0};
    t.v[1] = (phys_vec3_t){x1, y1, z1};
    t.v[2] = (phys_vec3_t){x2, y2, z2};
    return t;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/** NULL inputs should not crash, produce empty BVH. */
static int test_mesh_bvh_null_safe(void) {
    phys_mesh_bvh_t bvh;
    phys_frame_arena_t arena;
    setup_arena(&arena);

    /* NULL output. */
    phys_mesh_bvh_build(NULL, NULL, 0, &arena);

    /* NULL arena with items — should produce empty. */
    phys_triangle_t tri = make_tri(0,0,0, 1,0,0, 0,1,0);
    phys_mesh_bvh_build(&bvh, &tri, 1, NULL);
    ASSERT_UINT_EQ(0, bvh.node_count);

    /* NULL triangles with count > 0. */
    phys_mesh_bvh_build(&bvh, NULL, 5, &arena);
    ASSERT_UINT_EQ(0, bvh.node_count);

    /* Query on empty BVH. */
    phys_aabb_t query = {.min = {-1,-1,-1}, .max = {1,1,1}};
    uint32_t ids[4];
    uint32_t n = phys_mesh_bvh_query_aabb(&bvh, &query, ids, 4);
    ASSERT_UINT_EQ(0, n);

    /* Query with NULL BVH. */
    n = phys_mesh_bvh_query_aabb(NULL, &query, ids, 4);
    ASSERT_UINT_EQ(0, n);

    teardown_arena(&arena);
    return 0;
}

/** Empty mesh (zero triangles). */
static int test_mesh_bvh_empty(void) {
    phys_mesh_bvh_t bvh;
    phys_frame_arena_t arena;
    setup_arena(&arena);

    phys_mesh_bvh_build(&bvh, NULL, 0, &arena);
    ASSERT_UINT_EQ(0, bvh.node_count);
    ASSERT_UINT_EQ(UINT32_MAX, bvh.root);

    teardown_arena(&arena);
    return 0;
}

/** Single triangle produces one leaf node. */
static int test_mesh_bvh_single(void) {
    phys_mesh_bvh_t bvh;
    phys_frame_arena_t arena;
    setup_arena(&arena);

    phys_triangle_t tri = make_tri(0,0,0, 1,0,0, 0,1,0);
    phys_mesh_bvh_build(&bvh, &tri, 1, &arena);

    ASSERT_UINT_EQ(1, bvh.node_count);
    ASSERT_UINT_EQ(0, bvh.root);
    ASSERT_TRUE(phys_mesh_bvh_node_is_leaf(&bvh.nodes[0]));
    ASSERT_UINT_EQ(0, bvh.nodes[0].tri_index);

    /* AABB should contain the triangle. */
    ASSERT_TRUE(bvh.nodes[0].bounds.min.x <= 0.0f);
    ASSERT_TRUE(bvh.nodes[0].bounds.min.y <= 0.0f);
    ASSERT_TRUE(bvh.nodes[0].bounds.max.x >= 1.0f);
    ASSERT_TRUE(bvh.nodes[0].bounds.max.y >= 1.0f);

    /* Query should find it. */
    phys_aabb_t query = {.min = {-0.5f,-0.5f,-0.5f}, .max = {0.5f,0.5f,0.5f}};
    uint32_t ids[4];
    uint32_t n = phys_mesh_bvh_query_aabb(&bvh, &query, ids, 4);
    ASSERT_UINT_EQ(1, n);
    ASSERT_UINT_EQ(0, ids[0]);

    teardown_arena(&arena);
    return 0;
}

/** Two triangles: 1 internal node + 2 leaves = 3 nodes. */
static int test_mesh_bvh_two(void) {
    phys_mesh_bvh_t bvh;
    phys_frame_arena_t arena;
    setup_arena(&arena);

    phys_triangle_t tris[2] = {
        make_tri(-5,0,0, -4,0,0, -5,1,0),
        make_tri( 4,0,0,  5,0,0,  4,1,0),
    };
    phys_mesh_bvh_build(&bvh, tris, 2, &arena);

    ASSERT_UINT_EQ(3, bvh.node_count);
    ASSERT_UINT_EQ(0, bvh.root);

    /* Root is internal. */
    ASSERT_TRUE(!phys_mesh_bvh_node_is_leaf(&bvh.nodes[0]));

    /* Both children are leaves. */
    ASSERT_TRUE(phys_mesh_bvh_node_is_leaf(&bvh.nodes[bvh.nodes[0].left]));
    ASSERT_TRUE(phys_mesh_bvh_node_is_leaf(&bvh.nodes[bvh.nodes[0].right]));

    teardown_arena(&arena);
    return 0;
}

/** Many triangles: verify tree invariants. */
static int test_mesh_bvh_many_invariants(void) {
    phys_mesh_bvh_t bvh;
    phys_frame_arena_t arena;
    setup_arena(&arena);

    /* Grid of 100 triangles on the XZ plane. */
    const uint32_t N = 100;
    phys_triangle_t tris[100];
    for (uint32_t i = 0; i < N; i++) {
        float x = (float)(i % 10) * 2.0f;
        float z = (float)(i / 10) * 2.0f;
        tris[i] = make_tri(x, 0, z, x+1, 0, z, x, 0, z+1);
    }

    phys_mesh_bvh_build(&bvh, tris, N, &arena);

    /* Node count: at most 2N - 1 for N leaves. */
    ASSERT_TRUE(bvh.node_count <= 2 * N - 1);
    ASSERT_TRUE(bvh.node_count > 0);

    /* Count leaves — should equal N. */
    uint32_t leaf_count = 0;
    for (uint32_t i = 0; i < bvh.node_count; i++) {
        if (phys_mesh_bvh_node_is_leaf(&bvh.nodes[i])) {
            leaf_count++;
            /* Leaf tri_index must be valid. */
            ASSERT_TRUE(bvh.nodes[i].tri_index < N);
        }
    }
    ASSERT_UINT_EQ(N, leaf_count);

    /* Root bounds should contain all triangles. */
    const phys_aabb_t *root_bounds = &bvh.nodes[bvh.root].bounds;
    ASSERT_TRUE(root_bounds->min.x <= 0.0f);
    ASSERT_TRUE(root_bounds->min.z <= 0.0f);
    ASSERT_TRUE(root_bounds->max.x >= 10.0f);  /* 9*2 + 1 = 19, actually */
    ASSERT_TRUE(root_bounds->max.z >= 10.0f);

    teardown_arena(&arena);
    return 0;
}

/** SAH quality: tree height is bounded by O(log N). */
static int test_mesh_bvh_height_bounded(void) {
    phys_mesh_bvh_t bvh;
    phys_frame_arena_t arena;
    setup_arena(&arena);

    const uint32_t N = 256;
    phys_triangle_t tris[256];
    for (uint32_t i = 0; i < N; i++) {
        float x = (float)(i % 16) * 3.0f;
        float z = (float)(i / 16) * 3.0f;
        tris[i] = make_tri(x, 0, z, x+1, 0, z, x, 0, z+1);
    }

    phys_mesh_bvh_build(&bvh, tris, N, &arena);

    /* Compute height via iterative DFS. */
    typedef struct { uint32_t idx; uint32_t depth; } dfs_entry_t;
    uint32_t max_depth = 0;
    dfs_entry_t stack[512];
    uint32_t sp = 0;
    stack[sp].idx = bvh.root; stack[sp].depth = 0; sp++;
    while (sp > 0) {
        uint32_t idx = stack[sp-1].idx;
        uint32_t depth = stack[sp-1].depth;
        sp--;
        if (depth > max_depth) max_depth = depth;
        const phys_mesh_bvh_node_t *n = &bvh.nodes[idx];
        if (!phys_mesh_bvh_node_is_leaf(n)) {
            stack[sp].idx = n->left;  stack[sp].depth = depth + 1; sp++;
            stack[sp].idx = n->right; stack[sp].depth = depth + 1; sp++;
        }
    }

    /* Height should be ≤ 3 * log2(N) for a decent SAH build. */
    float max_expected = 3.0f * log2f((float)N);
    ASSERT_TRUE(max_depth <= (uint32_t)max_expected);

    teardown_arena(&arena);
    return 0;
}

/** AABB query: find triangles overlapping a region. */
static int test_mesh_bvh_query_aabb(void) {
    phys_mesh_bvh_t bvh;
    phys_frame_arena_t arena;
    setup_arena(&arena);

    /* 4 triangles at known positions. */
    phys_triangle_t tris[4] = {
        make_tri(0,0,0, 1,0,0, 0,1,0),     /* near origin */
        make_tri(10,0,0, 11,0,0, 10,1,0),   /* at x=10 */
        make_tri(0,0,10, 1,0,10, 0,1,10),   /* at z=10 */
        make_tri(10,0,10, 11,0,10, 10,1,10), /* at x=10,z=10 */
    };

    phys_mesh_bvh_build(&bvh, tris, 4, &arena);

    /* Query around origin — should find only tri 0. */
    phys_aabb_t query = {.min = {-0.5f,-0.5f,-0.5f}, .max = {0.5f,0.5f,0.5f}};
    uint32_t ids[8];
    uint32_t n = phys_mesh_bvh_query_aabb(&bvh, &query, ids, 8);
    ASSERT_UINT_EQ(1, n);
    ASSERT_UINT_EQ(0, ids[0]);

    /* Query large area — should find all 4. */
    phys_aabb_t big = {.min = {-1,-1,-1}, .max = {12,2,12}};
    n = phys_mesh_bvh_query_aabb(&bvh, &big, ids, 8);
    ASSERT_UINT_EQ(4, n);

    teardown_arena(&arena);
    return 0;
}

/** AABB query miss — no overlap. */
static int test_mesh_bvh_query_miss(void) {
    phys_mesh_bvh_t bvh;
    phys_frame_arena_t arena;
    setup_arena(&arena);

    phys_triangle_t tri = make_tri(0,0,0, 1,0,0, 0,1,0);
    phys_mesh_bvh_build(&bvh, &tri, 1, &arena);

    /* Query far away. */
    phys_aabb_t query = {.min = {100,100,100}, .max = {200,200,200}};
    uint32_t ids[4];
    uint32_t n = phys_mesh_bvh_query_aabb(&bvh, &query, ids, 4);
    ASSERT_UINT_EQ(0, n);

    teardown_arena(&arena);
    return 0;
}

/** Degenerate triangles (zero area) should still build without crash. */
static int test_mesh_bvh_degenerate(void) {
    phys_mesh_bvh_t bvh;
    phys_frame_arena_t arena;
    setup_arena(&arena);

    /* Collinear points → zero-area triangle. */
    phys_triangle_t tris[3] = {
        make_tri(0,0,0, 1,0,0, 2,0,0),  /* degenerate */
        make_tri(0,0,0, 0,0,0, 0,0,0),  /* point */
        make_tri(5,0,0, 6,0,0, 5,1,0),  /* valid */
    };

    phys_mesh_bvh_build(&bvh, tris, 3, &arena);
    ASSERT_TRUE(bvh.node_count > 0);

    /* All three should still be in the BVH as leaves. */
    uint32_t leaf_count = 0;
    for (uint32_t i = 0; i < bvh.node_count; i++) {
        if (phys_mesh_bvh_node_is_leaf(&bvh.nodes[i])) leaf_count++;
    }
    ASSERT_UINT_EQ(3, leaf_count);

    teardown_arena(&arena);
    return 0;
}

/** Coplanar triangles — all on the same plane. SAH should still split. */
static int test_mesh_bvh_coplanar(void) {
    phys_mesh_bvh_t bvh;
    phys_frame_arena_t arena;
    setup_arena(&arena);

    /* 16 triangles on the Y=0 plane, spread along X. */
    phys_triangle_t tris[16];
    for (int i = 0; i < 16; i++) {
        float x = (float)i * 5.0f;
        tris[i] = make_tri(x, 0, 0, x+1, 0, 0, x, 0, 1);
    }

    phys_mesh_bvh_build(&bvh, tris, 16, &arena);
    ASSERT_TRUE(bvh.node_count > 0);

    /* Query left half — should find ~8 triangles. */
    phys_aabb_t left = {.min = {-1,-1,-1}, .max = {37,1,2}};
    uint32_t ids[32];
    uint32_t n = phys_mesh_bvh_query_aabb(&bvh, &left, ids, 32);
    ASSERT_TRUE(n >= 8);

    teardown_arena(&arena);
    return 0;
}

/** Triangle AABB computation helper test. */
static int test_triangle_aabb(void) {
    phys_triangle_t tri = make_tri(-1, 2, -3, 4, -5, 6, 7, 8, -9);
    phys_aabb_t aabb = phys_triangle_aabb(&tri);

    ASSERT_TRUE(fabsf(aabb.min.x - (-1.0f)) < 1e-6f);
    ASSERT_TRUE(fabsf(aabb.min.y - (-5.0f)) < 1e-6f);
    ASSERT_TRUE(fabsf(aabb.min.z - (-9.0f)) < 1e-6f);
    ASSERT_TRUE(fabsf(aabb.max.x - 7.0f) < 1e-6f);
    ASSERT_TRUE(fabsf(aabb.max.y - 8.0f) < 1e-6f);
    ASSERT_TRUE(fabsf(aabb.max.z - 6.0f) < 1e-6f);

    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    printf("RUN p107_mesh_collider_bvh_tests\n");

    int fail_count = 0;
    int test_count = 0;

    RUN_TEST(test_mesh_bvh_null_safe);
    RUN_TEST(test_mesh_bvh_empty);
    RUN_TEST(test_mesh_bvh_single);
    RUN_TEST(test_mesh_bvh_two);
    RUN_TEST(test_mesh_bvh_many_invariants);
    RUN_TEST(test_mesh_bvh_height_bounded);
    RUN_TEST(test_mesh_bvh_query_aabb);
    RUN_TEST(test_mesh_bvh_query_miss);
    RUN_TEST(test_mesh_bvh_degenerate);
    RUN_TEST(test_mesh_bvh_coplanar);
    RUN_TEST(test_triangle_aabb);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
