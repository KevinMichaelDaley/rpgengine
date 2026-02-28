/**
 * @file mesh_uv_tests.c
 * @brief Tests for UV projection methods.
 */
#include <stdio.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_uv.h"
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

/* ------------------------------------------------------------------ */
/* Planar projection                                                   */
/* ------------------------------------------------------------------ */

static void test_planar_z(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0, 0, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1, 0, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1, 1, 0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    bool ok = mesh_uv_planar(&slot, &sel, MESH_AXIS_Z, 0);
    ASSERT(ok, "planar Z succeeded");

    /* UVs should be (0,0), (1,0), (1,1) — normalized from XY */
    ASSERT(fabsf(slot.uvs[0][0] - 0.0f) < 0.01f, "v0 u=0");
    ASSERT(fabsf(slot.uvs[0][1] - 0.0f) < 0.01f, "v0 v=0");
    ASSERT(fabsf(slot.uvs[0][2] - 1.0f) < 0.01f, "v1 u=1");
    ASSERT(fabsf(slot.uvs[0][3] - 0.0f) < 0.01f, "v1 v=0");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Box projection                                                      */
/* ------------------------------------------------------------------ */

static void test_box_projection(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    uint32_t fc = slot.index_count / 3;
    for (uint32_t f = 0; f < fc; f++) mesh_sel_bitset_set(&sel, f);

    bool ok = mesh_uv_box(&slot, &sel, 0);
    ASSERT(ok, "box projection succeeded");

    /* All UVs should be in [0,1] */
    bool all_valid = true;
    for (uint32_t v = 0; v < slot.vertex_count; v++) {
        float u = slot.uvs[0][v*2+0];
        float vv = slot.uvs[0][v*2+1];
        if (u < -0.01f || u > 1.01f || vv < -0.01f || vv > 1.01f) {
            all_valid = false;
            break;
        }
    }
    ASSERT(all_valid, "UVs in [0,1]");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Cylindrical projection                                              */
/* ------------------------------------------------------------------ */

static void test_cylindrical(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_cylinder(&slot, 1.0f, 2.0f, 16, MESH_AXIS_Y, (float[3]){0,0,0});

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    uint32_t fc = slot.index_count / 3;
    for (uint32_t f = 0; f < fc; f++) mesh_sel_bitset_set(&sel, f);

    bool ok = mesh_uv_cylindrical(&slot, &sel, MESH_AXIS_Y, 0);
    ASSERT(ok, "cylindrical succeeded");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Spherical projection                                                */
/* ------------------------------------------------------------------ */

static void test_spherical(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 512, 2048);
    mesh_prim_sphere(&slot, 1.0f, 8, (float[3]){0,0,0});

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    uint32_t fc = slot.index_count / 3;
    for (uint32_t f = 0; f < fc; f++) mesh_sel_bitset_set(&sel, f);

    bool ok = mesh_uv_spherical(&slot, &sel, 0);
    ASSERT(ok, "spherical succeeded");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Null / empty                                                        */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_uv_planar(NULL, NULL, MESH_AXIS_Z, 0), "null planar");
    ASSERT(!mesh_uv_box(NULL, NULL, 0), "null box");
    ASSERT(!mesh_uv_cylindrical(NULL, NULL, MESH_AXIS_Y, 0), "null cyl");
    ASSERT(!mesh_uv_spherical(NULL, NULL, 0), "null sph");
    g_pass++;
}

int main(void) {
    printf("mesh_uv_tests:\n");
    test_planar_z();
    test_box_projection();
    test_cylindrical();
    test_spherical();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
