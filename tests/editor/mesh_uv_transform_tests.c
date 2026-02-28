/**
 * @file mesh_uv_transform_tests.c
 * @brief Tests for UV transform commands.
 */
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "ferrum/editor/mesh/mesh_uv.h"
#include "ferrum/editor/mesh/mesh_uv_transform.h"
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

/* Helper: create a triangle with known UVs */
static void make_tri_(mesh_slot_t *slot) {
    mesh_slot_init(slot, 8, 24);
    float n[3] = {0,0,1};
    mesh_slot_add_vertex(slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(slot, (float[3]){1,1,0}, n);
    mesh_slot_add_triangle(slot, 0, 1, 2, 0);
    /* Set known UVs: (0,0), (1,0), (1,1) */
    slot->uvs[0][0] = 0.0f; slot->uvs[0][1] = 0.0f;
    slot->uvs[0][2] = 1.0f; slot->uvs[0][3] = 0.0f;
    slot->uvs[0][4] = 1.0f; slot->uvs[0][5] = 1.0f;
}

/* ------------------------------------------------------------------ */
/* Test: shift UVs                                                     */
/* ------------------------------------------------------------------ */

static void test_shift(void) {
    mesh_slot_t slot;
    make_tri_(&slot);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    bool ok = mesh_uv_shift(&slot, &sel, 0.5f, 0.25f, 0);
    ASSERT(ok, "shift succeeded");

    /* v0: (0+0.5, 0+0.25) = (0.5, 0.25) */
    ASSERT(fabsf(slot.uvs[0][0] - 0.5f) < 0.01f, "v0 u shifted");
    ASSERT(fabsf(slot.uvs[0][1] - 0.25f) < 0.01f, "v0 v shifted");
    /* v1: (1+0.5, 0+0.25) = (1.5, 0.25) */
    ASSERT(fabsf(slot.uvs[0][2] - 1.5f) < 0.01f, "v1 u shifted");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: rotate UVs 90 degrees                                         */
/* ------------------------------------------------------------------ */

static void test_rotate_90(void) {
    mesh_slot_t slot;
    make_tri_(&slot);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    /* Rotate 90° around centroid */
    float angle = (float)(M_PI / 2.0);
    bool ok = mesh_uv_rotate(&slot, &sel, angle, -1.0f, -1.0f, 0);
    ASSERT(ok, "rotate succeeded");

    /* Just check that UVs changed (exact values depend on centroid) */
    ASSERT(fabsf(slot.uvs[0][0] - 0.0f) > 0.01f ||
           fabsf(slot.uvs[0][1] - 0.0f) > 0.01f, "v0 moved after rotation");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: scale UVs 2x                                                  */
/* ------------------------------------------------------------------ */

static void test_scale_2x(void) {
    mesh_slot_t slot;
    make_tri_(&slot);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    /* Scale 2x from centroid */
    bool ok = mesh_uv_scale(&slot, &sel, 2.0f, 2.0f, -1.0f, -1.0f, 0);
    ASSERT(ok, "scale succeeded");

    /* UV range should now be 2x wider */
    float u_min = slot.uvs[0][0], u_max = slot.uvs[0][0];
    for (int i = 0; i < 3; i++) {
        float u = slot.uvs[0][i*2];
        if (u < u_min) u_min = u;
        if (u > u_max) u_max = u;
    }
    ASSERT(fabsf((u_max - u_min) - 2.0f) < 0.01f, "U range doubled");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: fit UVs to [0,1]                                              */
/* ------------------------------------------------------------------ */

static void test_fit(void) {
    mesh_slot_t slot;
    make_tri_(&slot);

    /* Offset UVs outside [0,1] */
    slot.uvs[0][0] = 2.0f; slot.uvs[0][1] = 3.0f;
    slot.uvs[0][2] = 4.0f; slot.uvs[0][3] = 3.0f;
    slot.uvs[0][4] = 4.0f; slot.uvs[0][5] = 5.0f;

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    bool ok = mesh_uv_fit(&slot, &sel, 0);
    ASSERT(ok, "fit succeeded");

    /* All UVs should now be in [0,1] */
    for (int i = 0; i < 3; i++) {
        float u = slot.uvs[0][i*2];
        float v = slot.uvs[0][i*2+1];
        ASSERT(u >= -0.01f && u <= 1.01f, "u in range");
        ASSERT(v >= -0.01f && v <= 1.01f, "v in range");
    }

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: grid snap                                                     */
/* ------------------------------------------------------------------ */

static void test_grid_snap(void) {
    mesh_slot_t slot;
    make_tri_(&slot);

    /* Set UVs to off-grid values */
    slot.uvs[0][0] = 0.13f; slot.uvs[0][1] = 0.27f;
    slot.uvs[0][2] = 0.62f; slot.uvs[0][3] = 0.88f;
    slot.uvs[0][4] = 0.37f; slot.uvs[0][5] = 0.51f;

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    bool ok = mesh_uv_grid_snap(&slot, &sel, 0.25f, 0);
    ASSERT(ok, "grid snap succeeded");

    /* Should snap to nearest 0.25 */
    ASSERT(fabsf(slot.uvs[0][0] - 0.25f) < 0.01f, "v0 u snapped to 0.25");
    ASSERT(fabsf(slot.uvs[0][1] - 0.25f) < 0.01f, "v0 v snapped to 0.25");
    ASSERT(fabsf(slot.uvs[0][2] - 0.5f) < 0.01f, "v1 u snapped to 0.5");
    ASSERT(fabsf(slot.uvs[0][3] - 1.0f) < 0.01f, "v1 v snapped to 1.0");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_uv_shift(NULL, NULL, 0, 0, 0), "null shift");
    ASSERT(!mesh_uv_rotate(NULL, NULL, 0, 0, 0, 0), "null rotate");
    ASSERT(!mesh_uv_scale(NULL, NULL, 1, 1, 0, 0, 0), "null scale");
    ASSERT(!mesh_uv_fit(NULL, NULL, 0), "null fit");
    ASSERT(!mesh_uv_grid_snap(NULL, NULL, 1, 0), "null grid");
    g_pass++;
}

int main(void) {
    printf("mesh_uv_transform_tests:\n");
    test_shift();
    test_rotate_90();
    test_scale_2x();
    test_fit();
    test_grid_snap();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
