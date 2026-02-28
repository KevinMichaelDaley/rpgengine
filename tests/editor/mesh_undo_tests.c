/**
 * @file mesh_undo_tests.c
 * @brief Tests for mesh undo/redo system.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "ferrum/editor/mesh/mesh_undo.h"
#include "ferrum/editor/mesh/mesh_primitives.h"
#include "ferrum/editor/mesh/mesh_edit.h"
#include "ferrum/editor/mesh/mesh_extrude.h"

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
/* Test: push + undo restores state                                    */
/* ------------------------------------------------------------------ */

static void test_undo_basic(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_undo_stack_t stack;
    mesh_undo_stack_init(&stack);

    /* Push pre-modification state */
    uint32_t orig_vc = slot.vertex_count;
    uint32_t orig_ic = slot.index_count;
    mesh_undo_push(&stack, &slot);

    /* Modify: add a vertex */
    float n[3] = {0,1,0};
    mesh_slot_add_vertex(&slot, (float[3]){5,5,5}, n);
    ASSERT(slot.vertex_count == orig_vc + 1, "vertex added");

    /* Push post-modification state */
    mesh_undo_push(&stack, &slot);

    /* Undo → restores pre-modification state */
    bool ok = mesh_undo(&stack, &slot);
    ASSERT(ok, "undo succeeded");
    ASSERT(slot.vertex_count == orig_vc, "vertex count restored");
    ASSERT(slot.index_count == orig_ic, "index count restored");

    mesh_undo_stack_destroy(&stack);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: redo re-applies                                               */
/* ------------------------------------------------------------------ */

static void test_redo(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_undo_stack_t stack;
    mesh_undo_stack_init(&stack);

    /* Push original state */
    mesh_undo_push(&stack, &slot);
    uint32_t orig_vc = slot.vertex_count;

    /* Modify and push modified state */
    float n[3] = {0,1,0};
    mesh_slot_add_vertex(&slot, (float[3]){5,5,5}, n);
    uint32_t mod_vc = slot.vertex_count;
    mesh_undo_push(&stack, &slot);

    /* Undo → back to original */
    mesh_undo(&stack, &slot);
    ASSERT(slot.vertex_count == orig_vc, "at original after undo");

    /* Redo → forward to modified */
    bool ok = mesh_redo(&stack, &slot);
    ASSERT(ok, "redo succeeded");
    ASSERT(slot.vertex_count == mod_vc, "redo restored modification");

    mesh_undo_stack_destroy(&stack);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: multiple undo chain                                           */
/* ------------------------------------------------------------------ */

static void test_undo_chain(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);

    float n[3] = {0,0,1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_undo_stack_t stack;
    mesh_undo_stack_init(&stack);

    /* State 0: 3 verts */
    mesh_undo_push(&stack, &slot);

    /* State 1: 4 verts */
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_undo_push(&stack, &slot);

    /* State 2: 5 verts */
    mesh_slot_add_vertex(&slot, (float[3]){2,0,0}, n);
    mesh_undo_push(&stack, &slot);

    ASSERT(slot.vertex_count == 5, "at state 2");

    /* Undo to state 2 snapshot (which stored state when vc=5) */
    mesh_undo(&stack, &slot);
    /* Undo to state 1 snapshot */
    mesh_undo(&stack, &slot);
    /* Undo to state 0 snapshot */
    mesh_undo(&stack, &slot);
    ASSERT(slot.vertex_count == 3, "back to state 0");

    mesh_undo_stack_destroy(&stack);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: undo on empty stack returns false                              */
/* ------------------------------------------------------------------ */

static void test_empty_undo(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);

    mesh_undo_stack_t stack;
    mesh_undo_stack_init(&stack);

    ASSERT(!mesh_undo(&stack, &slot), "no undo on empty");
    ASSERT(!mesh_redo(&stack, &slot), "no redo on empty");

    mesh_undo_stack_destroy(&stack);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_undo(NULL, NULL), "null undo");
    ASSERT(!mesh_redo(NULL, NULL), "null redo");
    mesh_undo_push(NULL, NULL); /* should not crash */
    g_pass++;
}

int main(void) {
    printf("mesh_undo_tests:\n");
    test_undo_basic();
    test_redo();
    test_undo_chain();
    test_empty_undo();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
