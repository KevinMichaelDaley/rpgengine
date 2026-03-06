/**
 * @file p004_renderer_scene_graph_tests.c
 * @brief Unit tests for the LCRS scene graph system.
 *
 * Tests cover:
 * - Lifecycle (init/destroy)
 * - Attach/detach operations and LCRS tree consistency
 * - Dirty flag propagation and world transform updates
 * - Edge cases (static nodes, double-attach, out-of-range)
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ferrum/renderer/scene/scene_node.h"
#include "ferrum/renderer/scene/scene_graph.h"
#include "ferrum/math/mat4.h"

/* ── Minimal test harness ────────────────────────────────────────── */

static int g_pass, g_fail;

#define RUN(fn) do {                                    \
    printf("RUN  " #fn "\n");                           \
    int rc = fn();                                      \
    if (rc == 0) { printf("  OK " #fn "\n"); g_pass++; } \
    else { printf("FAIL " #fn "\n"); g_fail++; }        \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  ASSERT_EQ failed: %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
        return 1; \
    } \
} while (0)

#define SENTINEL SCENE_NODE_NONE

/* Helper: check that two mat4 are approximately equal. */
static int mat4_approx_eq(mat4_t a, mat4_t b, float eps) {
    for (int i = 0; i < 16; i++) {
        if (fabsf(a.m[i] - b.m[i]) > eps) return 0;
    }
    return 1;
}

/* ── Tests ───────────────────────────────────────────────────────── */

/* 1. Init and destroy lifecycle. */
static int test_init_destroy(void) {
    scene_graph_t graph;
    int rc = scene_graph_init(&graph, 1024);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(graph.nodes != NULL);
    ASSERT_TRUE(graph.capacity == 1024);
    ASSERT_TRUE(graph.dirty_count == 0);

    /* All nodes should start as unattached. */
    for (uint32_t i = 0; i < 1024; i++) {
        ASSERT_EQ(graph.nodes[i].parent, SENTINEL);
        ASSERT_EQ(graph.nodes[i].first_child, SENTINEL);
        ASSERT_EQ(graph.nodes[i].next_sibling, SENTINEL);
    }

    scene_graph_destroy(&graph);
    ASSERT_TRUE(graph.nodes == NULL);
    ASSERT_TRUE(graph.capacity == 0);
    return 0;
}

/* 2. Attach entity as child of root. */
static int test_attach_to_root(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    /* Entity 5 becomes a root node (parent = SENTINEL). */
    int rc = scene_graph_attach(&graph, 5, SENTINEL);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(graph.nodes[5].parent, SENTINEL);

    /* Entity 10 becomes child of entity 5. */
    rc = scene_graph_attach(&graph, 10, 5);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(graph.nodes[10].parent, 5u);
    ASSERT_EQ(graph.nodes[5].first_child, 10u);
    ASSERT_EQ(graph.nodes[10].next_sibling, SENTINEL);

    scene_graph_destroy(&graph);
    return 0;
}

/* 3. Attach multiple children — LCRS sibling chain. */
static int test_sibling_chain(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    scene_graph_attach(&graph, 0, SENTINEL);  /* root */
    scene_graph_attach(&graph, 1, 0);         /* first child */
    scene_graph_attach(&graph, 2, 0);         /* second child */
    scene_graph_attach(&graph, 3, 0);         /* third child */

    /* first_child should be the most recently attached. */
    uint32_t fc = graph.nodes[0].first_child;
    ASSERT_TRUE(fc != SENTINEL);

    /* Walk siblings — should find exactly {1, 2, 3} in some order. */
    int found[4] = {0};
    uint32_t cursor = fc;
    int count = 0;
    while (cursor != SENTINEL && count < 10) {
        ASSERT_TRUE(cursor < 64);
        found[cursor] = 1;
        cursor = graph.nodes[cursor].next_sibling;
        count++;
    }
    ASSERT_EQ(count, 3);
    ASSERT_TRUE(found[1] && found[2] && found[3]);

    scene_graph_destroy(&graph);
    return 0;
}

/* 4. Multi-level hierarchy (grandchildren). */
static int test_grandchildren(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    scene_graph_attach(&graph, 0, SENTINEL);  /* root */
    scene_graph_attach(&graph, 1, 0);         /* child */
    scene_graph_attach(&graph, 2, 1);         /* grandchild */

    ASSERT_EQ(graph.nodes[0].first_child, 1u);
    ASSERT_EQ(graph.nodes[1].parent, 0u);
    ASSERT_EQ(graph.nodes[1].first_child, 2u);
    ASSERT_EQ(graph.nodes[2].parent, 1u);
    ASSERT_EQ(graph.nodes[2].first_child, SENTINEL);

    scene_graph_destroy(&graph);
    return 0;
}

/* 5. Mark dirty + update → world transforms propagate. */
static int test_transform_propagation(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    scene_graph_attach(&graph, 0, SENTINEL);
    scene_graph_attach(&graph, 1, 0);

    /* Set parent local transform to a translation (5, 0, 0). */
    graph.nodes[0].local_transform = mat4_translation(5.0f, 0.0f, 0.0f);
    scene_graph_mark_dirty(&graph, 0);

    /* Set child local transform to a translation (0, 3, 0). */
    graph.nodes[1].local_transform = mat4_translation(0.0f, 3.0f, 0.0f);
    scene_graph_mark_dirty(&graph, 1);

    /* Update the graph. */
    scene_graph_update(&graph);

    /* Root world = local (no parent). */
    mat4_t expected_root = mat4_translation(5.0f, 0.0f, 0.0f);
    ASSERT_TRUE(mat4_approx_eq(graph.nodes[0].world_transform, expected_root, 1e-5f));

    /* Child world = parent.world × child.local = translate(5,3,0). */
    mat4_t expected_child = mat4_translation(5.0f, 3.0f, 0.0f);
    ASSERT_TRUE(mat4_approx_eq(graph.nodes[1].world_transform, expected_child, 1e-5f));

    scene_graph_destroy(&graph);
    return 0;
}

/* 6. Dirty cascades to children. */
static int test_dirty_cascade(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    scene_graph_attach(&graph, 0, SENTINEL);
    scene_graph_attach(&graph, 1, 0);
    scene_graph_attach(&graph, 2, 1);

    /* Set transforms at each level. */
    graph.nodes[0].local_transform = mat4_translation(1.0f, 0.0f, 0.0f);
    graph.nodes[1].local_transform = mat4_translation(0.0f, 2.0f, 0.0f);
    graph.nodes[2].local_transform = mat4_translation(0.0f, 0.0f, 3.0f);

    /* Only mark root dirty — cascade should reach grandchild. */
    scene_graph_mark_dirty(&graph, 0);
    scene_graph_update(&graph);

    /* Grandchild world = translate(1, 2, 3). */
    mat4_t expected = mat4_translation(1.0f, 2.0f, 3.0f);
    ASSERT_TRUE(mat4_approx_eq(graph.nodes[2].world_transform, expected, 1e-5f));

    scene_graph_destroy(&graph);
    return 0;
}

/* 7. Detach entity — siblings relink. */
static int test_detach_middle_sibling(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    scene_graph_attach(&graph, 0, SENTINEL);
    scene_graph_attach(&graph, 1, 0);
    scene_graph_attach(&graph, 2, 0);
    scene_graph_attach(&graph, 3, 0);

    /* Detach entity 2. */
    scene_graph_detach(&graph, 2);

    /* Entity 2 should be fully unlinked. */
    ASSERT_EQ(graph.nodes[2].parent, SENTINEL);
    ASSERT_EQ(graph.nodes[2].first_child, SENTINEL);
    ASSERT_EQ(graph.nodes[2].next_sibling, SENTINEL);

    /* Remaining siblings {1, 3} should still be under parent 0. */
    uint32_t cursor = graph.nodes[0].first_child;
    int count = 0;
    int found_1 = 0, found_3 = 0;
    while (cursor != SENTINEL && count < 10) {
        if (cursor == 1) found_1 = 1;
        if (cursor == 3) found_3 = 1;
        ASSERT_TRUE(cursor != 2);  /* Must not find detached entity. */
        cursor = graph.nodes[cursor].next_sibling;
        count++;
    }
    ASSERT_EQ(count, 2);
    ASSERT_TRUE(found_1 && found_3);

    scene_graph_destroy(&graph);
    return 0;
}

/* 8. Detach root with children → children become roots. */
static int test_detach_root_reparents(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    scene_graph_attach(&graph, 0, SENTINEL);
    scene_graph_attach(&graph, 1, 0);
    scene_graph_attach(&graph, 2, 0);

    /* Detach entity 0 — children 1 and 2 should become roots. */
    scene_graph_detach(&graph, 0);

    ASSERT_EQ(graph.nodes[0].parent, SENTINEL);
    ASSERT_EQ(graph.nodes[0].first_child, SENTINEL);
    ASSERT_EQ(graph.nodes[1].parent, SENTINEL);
    ASSERT_EQ(graph.nodes[2].parent, SENTINEL);

    scene_graph_destroy(&graph);
    return 0;
}

/* 9. Detach leaf (no children) — no crash. */
static int test_detach_leaf(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    scene_graph_attach(&graph, 0, SENTINEL);
    scene_graph_attach(&graph, 1, 0);

    scene_graph_detach(&graph, 1);
    ASSERT_EQ(graph.nodes[1].parent, SENTINEL);
    ASSERT_EQ(graph.nodes[0].first_child, SENTINEL);

    scene_graph_destroy(&graph);
    return 0;
}

/* 10. Detach entity not attached — no crash. */
static int test_detach_unattached(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    /* Entity 5 was never attached — detach should be a no-op. */
    scene_graph_detach(&graph, 5);
    ASSERT_EQ(graph.nodes[5].parent, SENTINEL);

    scene_graph_destroy(&graph);
    return 0;
}

/* 11. Out-of-range index — attach rejected. */
static int test_attach_out_of_range(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    int rc = scene_graph_attach(&graph, 100, SENTINEL);
    ASSERT_TRUE(rc != 0);

    rc = scene_graph_attach(&graph, 0, 200);
    ASSERT_TRUE(rc != 0);

    scene_graph_destroy(&graph);
    return 0;
}

/* 12. Static nodes skip update. */
static int test_static_skip_update(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    scene_graph_attach(&graph, 0, SENTINEL);
    scene_graph_attach(&graph, 1, 0);

    /* Set child as static with a baked world transform. */
    graph.nodes[1].flags |= SCENE_NODE_STATIC;
    graph.nodes[1].world_transform = mat4_translation(99.0f, 99.0f, 99.0f);

    /* Move parent. */
    graph.nodes[0].local_transform = mat4_translation(10.0f, 0.0f, 0.0f);
    scene_graph_mark_dirty(&graph, 0);
    scene_graph_update(&graph);

    /* Static child should NOT have been updated. */
    mat4_t expected_static = mat4_translation(99.0f, 99.0f, 99.0f);
    ASSERT_TRUE(mat4_approx_eq(graph.nodes[1].world_transform, expected_static, 1e-5f));

    scene_graph_destroy(&graph);
    return 0;
}

/* 13. Double-attach (move entity to new parent). */
static int test_reattach_to_new_parent(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    scene_graph_attach(&graph, 0, SENTINEL);
    scene_graph_attach(&graph, 1, SENTINEL);
    scene_graph_attach(&graph, 2, 0);  /* child of 0 */

    /* Move entity 2 from parent 0 to parent 1. */
    scene_graph_attach(&graph, 2, 1);
    ASSERT_EQ(graph.nodes[2].parent, 1u);
    ASSERT_EQ(graph.nodes[1].first_child, 2u);
    /* Entity 0 should no longer have entity 2 as child. */
    ASSERT_EQ(graph.nodes[0].first_child, SENTINEL);

    scene_graph_destroy(&graph);
    return 0;
}

/* 14. Init with 0 capacity — fails gracefully. */
static int test_init_zero_capacity(void) {
    scene_graph_t graph;
    int rc = scene_graph_init(&graph, 0);
    ASSERT_TRUE(rc != 0);
    return 0;
}

/* 15. Null graph pointer — no crash. */
static int test_null_graph(void) {
    scene_graph_destroy(NULL);
    scene_graph_detach(NULL, 0);
    scene_graph_update(NULL);

    int rc = scene_graph_attach(NULL, 0, SENTINEL);
    ASSERT_TRUE(rc != 0);

    scene_graph_mark_dirty(NULL, 0);
    /* If we got here, no crash. */
    return 0;
}

/* 16. Update only processes dirty nodes, not entire tree. */
static int test_update_only_dirty(void) {
    scene_graph_t graph;
    scene_graph_init(&graph, 64);

    scene_graph_attach(&graph, 0, SENTINEL);
    scene_graph_attach(&graph, 1, SENTINEL);

    /* Set distinct local transforms. */
    graph.nodes[0].local_transform = mat4_translation(1.0f, 0.0f, 0.0f);
    graph.nodes[1].local_transform = mat4_translation(0.0f, 1.0f, 0.0f);

    /* Only mark node 0 dirty. */
    scene_graph_mark_dirty(&graph, 0);
    scene_graph_update(&graph);

    /* Node 0 should have been updated. */
    mat4_t expected_0 = mat4_translation(1.0f, 0.0f, 0.0f);
    ASSERT_TRUE(mat4_approx_eq(graph.nodes[0].world_transform, expected_0, 1e-5f));

    /* Node 1 was not dirty — world should still be identity (initial). */
    mat4_t ident = mat4_identity();
    ASSERT_TRUE(mat4_approx_eq(graph.nodes[1].world_transform, ident, 1e-5f));

    scene_graph_destroy(&graph);
    return 0;
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(void) {
    RUN(test_init_destroy);
    RUN(test_attach_to_root);
    RUN(test_sibling_chain);
    RUN(test_grandchildren);
    RUN(test_transform_propagation);
    RUN(test_dirty_cascade);
    RUN(test_detach_middle_sibling);
    RUN(test_detach_root_reparents);
    RUN(test_detach_leaf);
    RUN(test_detach_unattached);
    RUN(test_attach_out_of_range);
    RUN(test_static_skip_update);
    RUN(test_reattach_to_new_parent);
    RUN(test_init_zero_capacity);
    RUN(test_null_graph);
    RUN(test_update_only_dirty);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
