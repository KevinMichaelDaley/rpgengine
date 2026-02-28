/**
 * @file mesh_merge_tests.c
 * @brief Tests for vertex merge (weld) and edge/face collapse.
 */
#include <stdio.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_merge.h"
#include "ferrum/editor/mesh/mesh_edit.h"

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

static bool indices_valid_(const mesh_slot_t *slot) {
    for (uint32_t i = 0; i < slot->index_count; i++) {
        if (slot->indices[i] >= slot->vertex_count) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Merge to center                                                     */
/* ------------------------------------------------------------------ */

static void test_merge_center(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 48);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0, 0, 0}, n);  /* v0 */
    mesh_slot_add_vertex(&slot, (float[3]){2, 0, 0}, n);  /* v1 */
    mesh_slot_add_vertex(&slot, (float[3]){1, 2, 0}, n);  /* v2 */
    mesh_slot_add_vertex(&slot, (float[3]){1, 0, 0}, n);  /* v3 — between v0 and v1 */
    mesh_slot_add_triangle(&slot, 0, 3, 2, 0);
    mesh_slot_add_triangle(&slot, 3, 1, 2, 0);

    /* Select v0 and v3 (merge to center = (0.5, 0, 0)) */
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);
    mesh_sel_bitset_set(&sel, 3);

    bool ok = mesh_merge_vertices(&slot, &sel, MESH_MERGE_CENTER, NULL);
    ASSERT(ok, "merge center succeeded");
    ASSERT(indices_valid_(&slot), "indices valid");

    /* After merge: v3 is merged into v0 (or vice versa).
     * One of the triangles may become degenerate if v0==v3. */

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Merge to first                                                      */
/* ------------------------------------------------------------------ */

static void test_merge_first(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0, 0, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1, 0, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0.5f, 1, 0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    /* Select v0 and v1, merge to first (v0) */
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);
    mesh_sel_bitset_set(&sel, 1);

    bool ok = mesh_merge_vertices(&slot, &sel, MESH_MERGE_FIRST, NULL);
    ASSERT(ok, "merge first succeeded");

    /* Triangle becomes degenerate (v0 == v1), should be removed */
    ASSERT(slot.index_count == 0, "degenerate face removed");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Merge to cursor position                                            */
/* ------------------------------------------------------------------ */

static void test_merge_cursor(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0, 0, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1, 0, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0, 1, 0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);
    mesh_sel_bitset_set(&sel, 1);

    float cursor[3] = {5, 5, 5};
    bool ok = mesh_merge_vertices(&slot, &sel, MESH_MERGE_CURSOR, cursor);
    ASSERT(ok, "merge cursor succeeded");

    /* Surviving vertex should be at cursor pos */
    /* v0 is now at (5,5,5), face degenerate since v0==v1 */

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Threshold merge                                                     */
/* ------------------------------------------------------------------ */

static void test_merge_threshold(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 48);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0, 0, 0}, n);     /* v0 */
    mesh_slot_add_vertex(&slot, (float[3]){0.01f, 0, 0}, n); /* v1 — close to v0 */
    mesh_slot_add_vertex(&slot, (float[3]){1, 0, 0}, n);     /* v2 — far */
    mesh_slot_add_vertex(&slot, (float[3]){0.5f, 1, 0}, n);  /* v3 */
    mesh_slot_add_triangle(&slot, 0, 2, 3, 0);
    mesh_slot_add_triangle(&slot, 1, 2, 3, 0);

    float threshold = 0.05f;
    bool ok = mesh_merge_by_distance(&slot, threshold);
    ASSERT(ok, "threshold merge succeeded");
    ASSERT(indices_valid_(&slot), "indices valid");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Edge collapse                                                       */
/* ------------------------------------------------------------------ */

static void test_edge_collapse(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 48);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0, 0, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){2, 0, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1, 2, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1, -1, 0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);
    mesh_slot_add_triangle(&slot, 0, 3, 1, 0);

    /* Collapse edge (0, 1) */
    bool ok = mesh_collapse_edge(&slot, 0, 1);
    ASSERT(ok, "edge collapse succeeded");
    ASSERT(indices_valid_(&slot), "indices valid");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Empty / Null                                                        */
/* ------------------------------------------------------------------ */

static void test_empty_merge(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 4, 12);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    ASSERT(!mesh_merge_vertices(&slot, &sel, MESH_MERGE_CENTER, NULL), "empty merge");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_null_safety(void) {
    ASSERT(!mesh_merge_vertices(NULL, NULL, MESH_MERGE_CENTER, NULL), "null merge");
    ASSERT(!mesh_collapse_edge(NULL, 0, 0), "null collapse");
    g_pass++;
}

int main(void) {
    printf("mesh_merge_tests:\n");
    test_merge_center();
    test_merge_first();
    test_merge_cursor();
    test_merge_threshold();
    test_edge_collapse();
    test_empty_merge();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
