/**
 * @file p088_physics_static_bvh_build_tests.c
 * @brief Unit tests for Phase 6.1: Static BVH Build.
 *
 * Tests cover: NULL safety, empty/single/two-item builds, tree invariants,
 * and a basic "balanced enough" height check on a regular input set.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/static_bvh.h"

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
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n",\
                    __FILE__, __LINE__, (int)(exp), (int)(act));                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-60s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                               \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static int ceil_log2_u32(uint32_t x) {
    if (x <= 1) {
        return 0;
    }
    int p = 0;
    uint32_t v = 1;
    while (v < x) {
        v <<= 1u;
        p++;
    }
    return p;
}

static int aabb_contains(const phys_aabb_t *outer, const phys_aabb_t *inner) {
    if (!outer || !inner) {
        return 0;
    }
    return (outer->min.x <= inner->min.x && outer->min.y <= inner->min.y && outer->min.z <= inner->min.z &&
            outer->max.x >= inner->max.x && outer->max.y >= inner->max.y && outer->max.z >= inner->max.z);
}

static int compute_height(const phys_static_bvh_t *bvh, uint32_t node) {
    const phys_static_bvh_node_t *n = &bvh->nodes[node];
    if (phys_static_bvh_node_is_leaf(n)) {
        return 1;
    }
    int hl = compute_height(bvh, n->left);
    int hr = compute_height(bvh, n->right);
    return (hl > hr ? hl : hr) + 1;
}

static int validate_invariants(const phys_static_bvh_t *bvh,
                               const phys_aabb_t *items,
                               uint32_t item_count,
                               uint8_t *seen) {
    if (!bvh || !items || !seen) {
        return 1;
    }

    ASSERT_TRUE(bvh->node_count == 0 || bvh->nodes != NULL);
    if (item_count == 0) {
        ASSERT_INT_EQ(0, (int)bvh->node_count);
        return 0;
    }

    ASSERT_TRUE(bvh->root < bvh->node_count);

    /* Stack-based DFS to avoid deep recursion in pathological cases. */
    uint32_t stack[512];
    uint32_t sp = 0;
    stack[sp++] = bvh->root;

    while (sp) {
        uint32_t idx = stack[--sp];
        ASSERT_TRUE(idx < bvh->node_count);
        const phys_static_bvh_node_t *n = &bvh->nodes[idx];

        if (phys_static_bvh_node_is_leaf(n)) {
            ASSERT_TRUE(n->item_id < item_count);
            ASSERT_TRUE(!seen[n->item_id]);
            seen[n->item_id] = 1;

            /* Leaf bounds should contain the exact item AABB. */
            ASSERT_TRUE(aabb_contains(&n->bounds, &items[n->item_id]));
        } else {
            ASSERT_TRUE(n->left < bvh->node_count);
            ASSERT_TRUE(n->right < bvh->node_count);
            const phys_static_bvh_node_t *l = &bvh->nodes[n->left];
            const phys_static_bvh_node_t *r = &bvh->nodes[n->right];

            /* Parent bounds must contain both child bounds. */
            ASSERT_TRUE(aabb_contains(&n->bounds, &l->bounds));
            ASSERT_TRUE(aabb_contains(&n->bounds, &r->bounds));

            stack[sp++] = n->left;
            stack[sp++] = n->right;
            ASSERT_TRUE(sp < (uint32_t)(sizeof(stack) / sizeof(stack[0])));
        }
    }

    /* Every item must appear exactly once as a leaf. */
    for (uint32_t i = 0; i < item_count; i++) {
        ASSERT_TRUE(seen[i] == 1);
    }

    return 0;
}

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_static_bvh_build_null_safe(void) {
    phys_static_bvh_build(NULL, NULL, NULL, 0, NULL);

    phys_static_bvh_t bvh;
    memset(&bvh, 0xCC, sizeof(bvh));
    phys_static_bvh_build(&bvh, NULL, NULL, 0, NULL);
    return 0;
}

static int test_static_bvh_build_empty(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024u * 1024u);

    phys_static_bvh_t bvh;
    phys_static_bvh_build(&bvh, NULL, NULL, 0, &arena);

    ASSERT_INT_EQ(0, (int)bvh.node_count);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_static_bvh_build_single(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024u * 1024u);

    phys_aabb_t items[1] = {
        {.min = {0, 0, 0}, .max = {1, 2, 3}},
    };

    phys_static_bvh_t bvh;
    phys_static_bvh_build(&bvh, items, NULL, 1, &arena);

    ASSERT_INT_EQ(1, (int)bvh.node_count);
    ASSERT_INT_EQ(0, (int)bvh.root);
    ASSERT_TRUE(phys_static_bvh_node_is_leaf(&bvh.nodes[0]));
    ASSERT_INT_EQ(0, (int)bvh.nodes[0].item_id);
    ASSERT_TRUE(aabb_contains(&bvh.nodes[0].bounds, &items[0]));

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_static_bvh_build_two(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024u * 1024u);

    phys_aabb_t items[2] = {
        {.min = {0, 0, 0}, .max = {1, 1, 1}},
        {.min = {10, 0, 0}, .max = {11, 1, 1}},
    };

    phys_static_bvh_t bvh;
    phys_static_bvh_build(&bvh, items, NULL, 2, &arena);

    ASSERT_INT_EQ(3, (int)bvh.node_count);
    ASSERT_TRUE(!phys_static_bvh_node_is_leaf(&bvh.nodes[bvh.root]));

    uint8_t seen[2] = {0};
    ASSERT_INT_EQ(0, validate_invariants(&bvh, items, 2, seen));

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_static_bvh_build_balanced_enough(void) {
    enum { N = 256 };

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 8u * 1024u * 1024u);

    phys_aabb_t items[N];
    for (uint32_t i = 0; i < (uint32_t)N; i++) {
        float x = (float)i * 2.0f;
        items[i].min = (phys_vec3_t){x, 0.0f, 0.0f};
        items[i].max = (phys_vec3_t){x + 1.0f, 1.0f, 1.0f};
    }

    phys_static_bvh_t bvh;
    phys_static_bvh_build(&bvh, items, NULL, (uint32_t)N, &arena);

    ASSERT_INT_EQ((int)(2u * (uint32_t)N - 1u), (int)bvh.node_count);

    uint8_t seen[N];
    memset(seen, 0, sizeof(seen));
    ASSERT_INT_EQ(0, validate_invariants(&bvh, items, (uint32_t)N, seen));

    int h = compute_height(&bvh, bvh.root);
    int ideal = ceil_log2_u32((uint32_t)N) + 1;

    /* Allow some slack: SAH should not produce a degenerate chain here. */
    ASSERT_TRUE(h <= ideal * 2);

    phys_frame_arena_destroy(&arena);
    return 0;
}

int main(void) {
    printf("RUN p088_physics_static_bvh_build_tests\n");

    int fail_count = 0;
    int test_count = 0;

    RUN_TEST(test_static_bvh_build_null_safe);
    RUN_TEST(test_static_bvh_build_empty);
    RUN_TEST(test_static_bvh_build_single);
    RUN_TEST(test_static_bvh_build_two);
    RUN_TEST(test_static_bvh_build_balanced_enough);

    printf("\n%d/%d tests failed\n", fail_count, test_count);
    return fail_count ? 1 : 0;
}
