/**
 * @file mesh_extrude_tests.c
 * @brief Tests for face extrusion: single face, multi face, individual,
 *        direction override, empty selection.
 *
 * Test mesh: unit box (1,1,1) from mesh_prim_box.
 * Unit box: 24 verts, 36 indices (12 tris, 6 quads).
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_extrude.h"
#include "ferrum/editor/mesh/mesh_primitives.h"
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

/* Helper: all indices in range */
static bool indices_valid_(const mesh_slot_t *slot) {
    for (uint32_t i = 0; i < slot->index_count; i++) {
        if (slot->indices[i] >= slot->vertex_count) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Extrude single face                                                 */
/* ------------------------------------------------------------------ */

static void test_extrude_single_face(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    uint32_t orig_verts = slot.vertex_count;  /* 24 */
    uint32_t orig_faces = slot.index_count / 3; /* 12 */

    /* Select face 0 (first triangle of +Z face) and face 1 (second tri of +Z) */
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);
    mesh_sel_bitset_set(&sel, 1);

    bool ok = mesh_extrude(&slot, &sel, 0.5f, NULL);
    ASSERT(ok, "extrude succeeded");

    /* After extrude:
     * - Original 2 selected faces are replaced by 2 new offset faces
     * - 4 boundary edges create 4 side wall quads (8 tris)
     * - Total faces: (12 - 2) + 2 + 8 = 20 faces
     * - New vertices: 4 (duplicated boundary vertices) */
    ASSERT(slot.vertex_count > orig_verts, "vertices added");
    ASSERT(slot.index_count / 3 > orig_faces, "faces added");
    ASSERT(indices_valid_(&slot), "all indices valid");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Extrude with direction override                                     */
/* ------------------------------------------------------------------ */

static void test_extrude_direction(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);
    mesh_sel_bitset_set(&sel, 1);

    float dir[3] = {0, 1, 0}; /* extrude upward */
    bool ok = mesh_extrude(&slot, &sel, 0.5f, dir);
    ASSERT(ok, "direction extrude succeeded");
    ASSERT(indices_valid_(&slot), "indices valid");

    /* Check that some new vertices are offset in Y */
    bool found_up = false;
    for (uint32_t i = 0; i < slot.vertex_count; i++) {
        if (slot.positions[i*3+1] > 0.9f) { found_up = true; break; }
    }
    ASSERT(found_up, "vertices offset upward");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Extrude empty selection                                             */
/* ------------------------------------------------------------------ */

static void test_extrude_empty(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    uint32_t orig_faces = slot.index_count / 3;
    bool ok = mesh_extrude(&slot, &sel, 0.5f, NULL);
    ASSERT(!ok, "empty selection returns false");
    ASSERT(slot.index_count / 3 == orig_faces, "no faces added");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Extrude individual                                                  */
/* ------------------------------------------------------------------ */

static void test_extrude_individual(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);
    mesh_sel_bitset_set(&sel, 1);

    bool ok = mesh_extrude_individual(&slot, &sel, 0.3f);
    ASSERT(ok, "individual extrude succeeded");
    ASSERT(indices_valid_(&slot), "indices valid");

    /* Each face gets its own extrusion — no shared vertices between pillars */
    ASSERT(slot.vertex_count > 24, "new vertices created");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Single triangle extrude                                             */
/* ------------------------------------------------------------------ */

static void test_extrude_single_tri(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 32, 96);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    bool ok = mesh_extrude(&slot, &sel, 1.0f, NULL);
    ASSERT(ok, "single tri extrude succeeded");

    /* Single tri: 3 boundary edges → 3 side quads (6 tris) + 1 top face = 7 tris total */
    /* (original face is replaced, so 0 + 1 + 6 = 7 faces) */
    ASSERT(slot.index_count / 3 == 7, "7 faces after single tri extrude");
    ASSERT(indices_valid_(&slot), "indices valid");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Null safety                                                         */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_extrude(NULL, NULL, 0, NULL), "null slot");
    ASSERT(!mesh_extrude_individual(NULL, NULL, 0), "null individual");

    mesh_slot_t slot;
    mesh_slot_init(&slot, 4, 12);
    ASSERT(!mesh_extrude(&slot, NULL, 0, NULL), "null sel");
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(void) {
    printf("mesh_extrude_tests:\n");

    test_extrude_single_face();
    test_extrude_direction();
    test_extrude_empty();
    test_extrude_individual();
    test_extrude_single_tri();
    test_null_safety();

    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
