/**
 * @file mesh_subdivide_tests.c
 * @brief Tests for mesh subdivision: linear, Catmull-Clark, Loop.
 */
#include <stdio.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_subdivide.h"
#include "ferrum/editor/mesh/mesh_primitives.h"

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "  FAIL [%s:%d] %s (got %u)\n",           \
                    __FILE__, __LINE__, msg, (unsigned)_test_val_);    \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

#define ASSERTV(cond, msg, val)                                        \
    do {                                                               \
        unsigned _test_val_ = (unsigned)(val);                         \
        if (!(cond)) {                                                 \
            fprintf(stderr, "  FAIL [%s:%d] %s (got %u)\n",           \
                    __FILE__, __LINE__, msg, _test_val_);              \
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
/* Linear subdivision: single triangle → 4 triangles                   */
/* ------------------------------------------------------------------ */

static void test_linear_single_tri(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 32, 128);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){2,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,2,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    bool ok = mesh_subdivide_linear(&slot, 1);
    ASSERTV(ok, "linear subdiv succeeded", ok);
    ASSERTV(slot.index_count / 3 == 4, "4 faces after linear subdiv", slot.index_count/3);
    ASSERTV(indices_valid_(&slot), "indices valid", 0);

    /* New vertices should be edge midpoints */
    /* 3 original + 3 edge midpoints = 6 */
    ASSERTV(slot.vertex_count == 6, "6 verts after linear subdiv", slot.vertex_count);

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Linear 2 levels: 1 → 4 → 16                                        */
/* ------------------------------------------------------------------ */

static void test_linear_two_levels(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 128, 512);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    bool ok = mesh_subdivide_linear(&slot, 2);
    ASSERTV(ok, "2-level linear subdiv", ok);
    ASSERTV(slot.index_count / 3 == 16, "16 faces after 2 levels", slot.index_count/3);
    ASSERTV(indices_valid_(&slot), "indices valid", 0);

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Linear on box (12 tris → 48 tris)                                   */
/* ------------------------------------------------------------------ */

static void test_linear_box(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 512, 2048);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    uint32_t orig_faces = slot.index_count / 3; /* 12 */

    bool ok = mesh_subdivide_linear(&slot, 1);
    ASSERTV(ok, "linear box subdiv", ok);
    ASSERTV(slot.index_count / 3 == orig_faces * 4, "4x faces", slot.index_count/3);
    ASSERTV(indices_valid_(&slot), "indices valid", 0);

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Loop subdivision                                                    */
/* ------------------------------------------------------------------ */

static void test_loop_single_tri(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 32, 128);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){2,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,2,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    bool ok = mesh_subdivide_loop(&slot, 1);
    ASSERTV(ok, "loop subdiv succeeded", ok);
    /* Loop subdivision: 1 tri → 4 tris (same count as linear) */
    ASSERTV(slot.index_count / 3 == 4, "4 faces after loop", slot.index_count/3);
    ASSERTV(indices_valid_(&slot), "indices valid", 0);

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Empty mesh                                                          */
/* ------------------------------------------------------------------ */

static void test_empty_mesh(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 4, 12);

    unsigned _test_val_ = 0;
    ASSERT(!mesh_subdivide_linear(&slot, 1), "empty linear");
    ASSERT(!mesh_subdivide_loop(&slot, 1), "empty loop");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Null safety                                                         */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    unsigned _test_val_ = 0;
    ASSERT(!mesh_subdivide_linear(NULL, 1), "null linear");
    ASSERT(!mesh_subdivide_loop(NULL, 1), "null loop");
    g_pass++;
}

int main(void) {
    printf("mesh_subdivide_tests:\n");
    test_linear_single_tri();
    test_linear_two_levels();
    test_linear_box();
    test_loop_single_tri();
    test_empty_mesh();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
