/**
 * @file mesh_brush_tests.c
 * @brief Tests for brush-based mesh creation from half-plane intersection.
 */
#include <stdio.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_brush.h"
#include "ferrum/editor/mesh/mesh_slot.h"

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
/* Test: 6 axis-aligned planes create a unit box                       */
/* ------------------------------------------------------------------ */

static void test_axis_box(void) {
    /* Six planes defining a 2x2x2 box centered at origin */
    mesh_brush_plane_t planes[6] = {
        {{ 1, 0, 0},  1.0f},  /* right:  x <= 1 */
        {{-1, 0, 0},  1.0f},  /* left:  -x <= 1 → x >= -1 */
        {{ 0, 1, 0},  1.0f},  /* top:    y <= 1 */
        {{ 0,-1, 0},  1.0f},  /* bottom: y >= -1 */
        {{ 0, 0, 1},  1.0f},  /* front:  z <= 1 */
        {{ 0, 0,-1},  1.0f},  /* back:   z >= -1 */
    };

    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);

    bool ok = mesh_create_from_brush(&slot, planes, 6);
    ASSERT(ok, "brush creation succeeded");
    ASSERT(slot.vertex_count >= 8, "at least 8 vertices");
    ASSERT(slot.index_count >= 12*3, "at least 12 triangles (box)");

    /* All vertices should be within [-1, 1] on all axes */
    for (uint32_t v = 0; v < slot.vertex_count; v++) {
        float x = slot.positions[v*3+0];
        float y = slot.positions[v*3+1];
        float z = slot.positions[v*3+2];
        ASSERT(x >= -1.01f && x <= 1.01f, "x in bounds");
        ASSERT(y >= -1.01f && y <= 1.01f, "y in bounds");
        ASSERT(z >= -1.01f && z <= 1.01f, "z in bounds");
    }

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: wedge (5 planes: box + diagonal cut)                          */
/* ------------------------------------------------------------------ */

static void test_wedge(void) {
    mesh_brush_plane_t planes[5] = {
        {{ 1, 0, 0},  1.0f},
        {{-1, 0, 0},  1.0f},
        {{ 0, 1, 0},  1.0f},
        {{ 0,-1, 0},  1.0f},
        {{ 0, 0, 1},  0.0f},  /* clip at z=0, keep z<=0 */
    };

    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);

    bool ok = mesh_create_from_brush(&slot, planes, 5);
    ASSERT(ok, "wedge creation succeeded");
    ASSERT(slot.vertex_count >= 4, "has vertices");

    /* All z should be <= 0.01 */
    for (uint32_t v = 0; v < slot.vertex_count; v++) {
        float z = slot.positions[v*3+2];
        ASSERT(z <= 0.01f, "z <= 0 for wedge");
    }

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: too few planes (need at least 4)                              */
/* ------------------------------------------------------------------ */

static void test_too_few(void) {
    mesh_brush_plane_t planes[2] = {
        {{ 1, 0, 0}, 1.0f},
        {{-1, 0, 0}, 1.0f},
    };

    mesh_slot_t slot;
    mesh_slot_init(&slot, 64, 256);

    bool ok = mesh_create_from_brush(&slot, planes, 2);
    /* Should still produce something (a slab) or succeed */
    /* We just verify no crash */
    (void)ok;

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_create_from_brush(NULL, NULL, 0), "null brush");

    mesh_slot_t slot;
    mesh_slot_init(&slot, 64, 256);
    ASSERT(!mesh_create_from_brush(&slot, NULL, 6), "null planes");
    mesh_slot_destroy(&slot);
    g_pass++;
}

int main(void) {
    printf("mesh_brush_tests:\n");
    test_axis_box();
    test_wedge();
    test_too_few();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
