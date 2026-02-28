/**
 * @file mesh_csg_tests.c
 * @brief Tests for CSG operations (hollow, merge, subtract, intersect).
 */
#include <stdio.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_csg.h"
#include "ferrum/editor/mesh/mesh_primitives.h"

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
/* Test: hollow a box                                                  */
/* ------------------------------------------------------------------ */

static void test_hollow_box(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){2,2,2}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    uint32_t orig_verts = slot.vertex_count;
    uint32_t orig_indices = slot.index_count;

    bool ok = mesh_csg_hollow(&slot, 0.2f);
    ASSERT(ok, "hollow succeeded");

    /* Should have approximately double the geometry (outer + inner shell) */
    ASSERT(slot.vertex_count > orig_verts, "more vertices after hollow");
    ASSERT(slot.index_count > orig_indices, "more indices after hollow");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: merge two boxes                                               */
/* ------------------------------------------------------------------ */

static void test_merge_boxes(void) {
    mesh_slot_t a, b, result;
    mesh_slot_init(&a, 256, 1024);
    mesh_slot_init(&b, 256, 1024);
    mesh_slot_init(&result, 512, 2048);

    mesh_prim_box(&a, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});
    mesh_prim_box(&b, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0.5f,0,0});

    bool ok = mesh_csg_merge(&a, &b, &result);
    ASSERT(ok, "merge succeeded");
    ASSERT(result.vertex_count > 0, "has vertices");
    ASSERT(result.index_count > 0, "has indices");

    mesh_slot_destroy(&a);
    mesh_slot_destroy(&b);
    mesh_slot_destroy(&result);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: subtract box from box                                         */
/* ------------------------------------------------------------------ */

static void test_subtract(void) {
    mesh_slot_t target, cutter, result;
    mesh_slot_init(&target, 256, 1024);
    mesh_slot_init(&cutter, 256, 1024);
    mesh_slot_init(&result, 512, 2048);

    /* Large target box */
    mesh_prim_box(&target, (float[3]){2,2,2}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});
    /* Small cutter centered inside */
    mesh_prim_box(&cutter, (float[3]){0.5f,0.5f,0.5f}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    bool ok = mesh_csg_subtract(&target, &cutter, &result);
    ASSERT(ok, "subtract succeeded");
    ASSERT(result.vertex_count > 0, "has vertices");
    ASSERT(result.index_count > 0, "has indices");

    mesh_slot_destroy(&target);
    mesh_slot_destroy(&cutter);
    mesh_slot_destroy(&result);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: intersect two overlapping boxes                                */
/* ------------------------------------------------------------------ */

static void test_intersect(void) {
    mesh_slot_t a, b, result;
    mesh_slot_init(&a, 256, 1024);
    mesh_slot_init(&b, 256, 1024);
    mesh_slot_init(&result, 512, 2048);

    mesh_prim_box(&a, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});
    mesh_prim_box(&b, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0.5f,0,0});

    bool ok = mesh_csg_intersect(&a, &b, &result);
    ASSERT(ok, "intersect succeeded");
    ASSERT(result.vertex_count > 0, "has vertices");
    ASSERT(result.index_count > 0, "has indices");

    /* Intersection volume should be smaller than either input */
    ASSERT(result.vertex_count <= a.vertex_count + b.vertex_count,
           "bounded vertex count");

    mesh_slot_destroy(&a);
    mesh_slot_destroy(&b);
    mesh_slot_destroy(&result);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_csg_hollow(NULL, 1.0f), "null hollow");
    ASSERT(!mesh_csg_merge(NULL, NULL, NULL), "null merge");
    ASSERT(!mesh_csg_subtract(NULL, NULL, NULL), "null subtract");
    ASSERT(!mesh_csg_intersect(NULL, NULL, NULL), "null intersect");
    g_pass++;
}

int main(void) {
    printf("mesh_csg_tests:\n");
    test_hollow_box();
    test_merge_boxes();
    test_subtract();
    test_intersect();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
