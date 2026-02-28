/**
 * @file mesh_edit_tests.c
 * @brief Unit tests for mesh_edit_t — top-level mesh editing subsystem.
 *
 * Tests cover: init, destroy, active slot switching, selection mode,
 * selection bitset operations, edge cases.
 */
#include <stdio.h>
#include <string.h>
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

/* ------------------------------------------------------------------ */
/* Init / Destroy                                                      */
/* ------------------------------------------------------------------ */

static void test_init(void) {
    mesh_edit_t edit;
    bool ok = mesh_edit_init(&edit);
    ASSERT(ok, "init should succeed");
    ASSERT(edit.active_slot == 0, "active slot defaults to 0");
    ASSERT(edit.mode == MESH_SEL_MODE_FACE, "default mode is face");
    ASSERT(edit.sel_vertices.count == 0, "no vertices selected");
    ASSERT(edit.sel_edges.count == 0, "no edges selected");
    ASSERT(edit.sel_faces.count == 0, "no faces selected");
    mesh_edit_destroy(&edit);
    g_pass++;
}

static void test_init_null(void) {
    bool ok = mesh_edit_init(NULL);
    ASSERT(!ok, "init NULL should fail");
    g_pass++;
}

static void test_destroy_null(void) {
    /* Should not crash */
    mesh_edit_destroy(NULL);
    g_pass++;
}

static void test_slots_initialized(void) {
    mesh_edit_t edit;
    mesh_edit_init(&edit);

    /* All 16 slots should be initialized (empty but valid) */
    for (int i = 0; i < MESH_MAX_EDITABLE; i++) {
        ASSERT(edit.slots[i].vertex_count == 0, "slot empty");
        ASSERT(edit.slots[i].positions != NULL || edit.slots[i].vertex_capacity == 0,
               "slot valid");
    }

    mesh_edit_destroy(&edit);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Active slot                                                         */
/* ------------------------------------------------------------------ */

static void test_set_active_slot(void) {
    mesh_edit_t edit;
    mesh_edit_init(&edit);

    bool ok = mesh_edit_set_active_slot(&edit, 5);
    ASSERT(ok, "set active slot 5");
    ASSERT(edit.active_slot == 5, "active is 5");

    ok = mesh_edit_set_active_slot(&edit, 0);
    ASSERT(ok, "set back to 0");
    ASSERT(edit.active_slot == 0, "active is 0");

    mesh_edit_destroy(&edit);
    g_pass++;
}

static void test_set_active_slot_oob(void) {
    mesh_edit_t edit;
    mesh_edit_init(&edit);

    bool ok = mesh_edit_set_active_slot(&edit, MESH_MAX_EDITABLE);
    ASSERT(!ok, "OOB slot should fail");
    ASSERT(edit.active_slot == 0, "unchanged after OOB");

    ok = mesh_edit_set_active_slot(&edit, 255);
    ASSERT(!ok, "way OOB should fail");

    mesh_edit_destroy(&edit);
    g_pass++;
}

static void test_get_active_slot(void) {
    mesh_edit_t edit;
    mesh_edit_init(&edit);

    mesh_slot_t *slot = mesh_edit_get_active_slot(&edit);
    ASSERT(slot != NULL, "active slot should be non-NULL");
    ASSERT(slot == &edit.slots[0], "active is slot 0");

    mesh_edit_set_active_slot(&edit, 3);
    slot = mesh_edit_get_active_slot(&edit);
    ASSERT(slot == &edit.slots[3], "active is slot 3");

    mesh_edit_destroy(&edit);
    g_pass++;
}

static void test_get_active_slot_null(void) {
    mesh_slot_t *slot = mesh_edit_get_active_slot(NULL);
    ASSERT(slot == NULL, "NULL edit returns NULL slot");
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Selection mode                                                      */
/* ------------------------------------------------------------------ */

static void test_set_mode(void) {
    mesh_edit_t edit;
    mesh_edit_init(&edit);

    bool ok = mesh_edit_set_mode(&edit, MESH_SEL_MODE_VERTEX);
    ASSERT(ok, "set vertex mode");
    ASSERT(edit.mode == MESH_SEL_MODE_VERTEX, "mode is vertex");

    ok = mesh_edit_set_mode(&edit, MESH_SEL_MODE_EDGE);
    ASSERT(ok, "set edge mode");
    ASSERT(edit.mode == MESH_SEL_MODE_EDGE, "mode is edge");

    ok = mesh_edit_set_mode(&edit, MESH_SEL_MODE_POLYGROUP);
    ASSERT(ok, "set polygroup mode");
    ASSERT(edit.mode == MESH_SEL_MODE_POLYGROUP, "mode is polygroup");

    ok = mesh_edit_set_mode(&edit, MESH_SEL_MODE_OBJECT);
    ASSERT(ok, "set object mode");
    ASSERT(edit.mode == MESH_SEL_MODE_OBJECT, "mode is object");

    mesh_edit_destroy(&edit);
    g_pass++;
}

static void test_set_mode_invalid(void) {
    mesh_edit_t edit;
    mesh_edit_init(&edit);

    bool ok = mesh_edit_set_mode(&edit, 99);
    ASSERT(!ok, "invalid mode should fail");
    ASSERT(edit.mode == MESH_SEL_MODE_FACE, "mode unchanged");

    mesh_edit_destroy(&edit);
    g_pass++;
}

static void test_set_mode_clears_selection(void) {
    mesh_edit_t edit;
    mesh_edit_init(&edit);

    /* Add some face selections */
    mesh_sel_bitset_set(&edit.sel_faces, 0);
    mesh_sel_bitset_set(&edit.sel_faces, 5);
    ASSERT(edit.sel_faces.count == 2, "2 faces selected");

    /* Switching mode should clear all selections */
    mesh_edit_set_mode(&edit, MESH_SEL_MODE_VERTEX);
    ASSERT(edit.sel_faces.count == 0, "faces cleared on mode switch");
    ASSERT(edit.sel_vertices.count == 0, "vertices clear too");
    ASSERT(edit.sel_edges.count == 0, "edges clear too");

    mesh_edit_destroy(&edit);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Selection bitset                                                    */
/* ------------------------------------------------------------------ */

static void test_bitset_set_get(void) {
    mesh_sel_bitset_t bs;
    mesh_sel_bitset_init(&bs);

    mesh_sel_bitset_set(&bs, 0);
    mesh_sel_bitset_set(&bs, 7);
    mesh_sel_bitset_set(&bs, 100);

    ASSERT(mesh_sel_bitset_test(&bs, 0), "bit 0 set");
    ASSERT(mesh_sel_bitset_test(&bs, 7), "bit 7 set");
    ASSERT(mesh_sel_bitset_test(&bs, 100), "bit 100 set");
    ASSERT(!mesh_sel_bitset_test(&bs, 1), "bit 1 not set");
    ASSERT(!mesh_sel_bitset_test(&bs, 99), "bit 99 not set");
    ASSERT(bs.count == 3, "count is 3");

    mesh_sel_bitset_destroy(&bs);
    g_pass++;
}

static void test_bitset_clear_bit(void) {
    mesh_sel_bitset_t bs;
    mesh_sel_bitset_init(&bs);

    mesh_sel_bitset_set(&bs, 5);
    mesh_sel_bitset_set(&bs, 10);
    ASSERT(bs.count == 2, "2 bits set");

    mesh_sel_bitset_unset(&bs, 5);
    ASSERT(!mesh_sel_bitset_test(&bs, 5), "bit 5 cleared");
    ASSERT(mesh_sel_bitset_test(&bs, 10), "bit 10 still set");
    ASSERT(bs.count == 1, "count is 1");

    /* Clearing already-clear bit is a no-op */
    mesh_sel_bitset_unset(&bs, 5);
    ASSERT(bs.count == 1, "count still 1");

    mesh_sel_bitset_destroy(&bs);
    g_pass++;
}

static void test_bitset_clear_all(void) {
    mesh_sel_bitset_t bs;
    mesh_sel_bitset_init(&bs);

    mesh_sel_bitset_set(&bs, 0);
    mesh_sel_bitset_set(&bs, 50);
    mesh_sel_bitset_set(&bs, 200);
    ASSERT(bs.count == 3, "3 bits");

    mesh_sel_bitset_clear_all(&bs);
    ASSERT(bs.count == 0, "cleared");
    ASSERT(!mesh_sel_bitset_test(&bs, 0), "bit 0 clear");
    ASSERT(!mesh_sel_bitset_test(&bs, 50), "bit 50 clear");

    mesh_sel_bitset_destroy(&bs);
    g_pass++;
}

static void test_bitset_toggle(void) {
    mesh_sel_bitset_t bs;
    mesh_sel_bitset_init(&bs);

    mesh_sel_bitset_toggle(&bs, 3);
    ASSERT(mesh_sel_bitset_test(&bs, 3), "toggled on");
    ASSERT(bs.count == 1, "count 1");

    mesh_sel_bitset_toggle(&bs, 3);
    ASSERT(!mesh_sel_bitset_test(&bs, 3), "toggled off");
    ASSERT(bs.count == 0, "count 0");

    mesh_sel_bitset_destroy(&bs);
    g_pass++;
}

static void test_bitset_large_index(void) {
    mesh_sel_bitset_t bs;
    mesh_sel_bitset_init(&bs);

    /* Test with indices up to 65535 (max vertex count) */
    mesh_sel_bitset_set(&bs, 65535);
    ASSERT(mesh_sel_bitset_test(&bs, 65535), "large index set");
    ASSERT(bs.count == 1, "count 1");
    ASSERT(!mesh_sel_bitset_test(&bs, 65534), "neighbor not set");

    mesh_sel_bitset_destroy(&bs);
    g_pass++;
}

static void test_bitset_double_set(void) {
    mesh_sel_bitset_t bs;
    mesh_sel_bitset_init(&bs);

    mesh_sel_bitset_set(&bs, 10);
    mesh_sel_bitset_set(&bs, 10); /* double set */
    ASSERT(bs.count == 1, "count stays 1 on double set");

    mesh_sel_bitset_destroy(&bs);
    g_pass++;
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(void) {
    printf("mesh_edit_tests:\n");

    test_init();
    test_init_null();
    test_destroy_null();
    test_slots_initialized();
    test_set_active_slot();
    test_set_active_slot_oob();
    test_get_active_slot();
    test_get_active_slot_null();
    test_set_mode();
    test_set_mode_invalid();
    test_set_mode_clears_selection();
    test_bitset_set_get();
    test_bitset_clear_bit();
    test_bitset_clear_all();
    test_bitset_toggle();
    test_bitset_large_index();
    test_bitset_double_set();

    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
