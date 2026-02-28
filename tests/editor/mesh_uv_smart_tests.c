/**
 * @file mesh_uv_smart_tests.c
 * @brief Tests for smart UV unwrap (angle-based island detection + conformal flatten).
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "ferrum/editor/mesh/mesh_uv_smart.h"
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
/* Test: box produces 6 islands at 45° threshold                       */
/* ------------------------------------------------------------------ */

static void test_box_islands(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_uv_island_set_t islands;
    mesh_uv_island_set_init(&islands);

    /* 45° threshold should split all 6 box faces into separate islands
     * since adjacent box faces meet at 90°. */
    uint32_t count = mesh_uv_find_islands(&slot, &islands, (float)(45.0 * M_PI / 180.0));
    ASSERT(count == 6, "box has 6 islands at 45deg");

    /* Each island should have exactly 2 faces (each box side = 2 triangles) */
    for (uint32_t i = 0; i < count; i++) {
        ASSERT(islands.islands[i].face_count == 2, "island has 2 faces");
    }

    mesh_uv_island_set_destroy(&islands);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: high threshold produces fewer islands                         */
/* ------------------------------------------------------------------ */

static void test_threshold_sensitivity(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_uv_island_set_t islands;
    mesh_uv_island_set_init(&islands);

    /* 120° threshold — box faces meet at 90°, which is < 120°,
     * so no splitting: all faces in one island */
    uint32_t count = mesh_uv_find_islands(&slot, &islands, (float)(120.0 * M_PI / 180.0));
    ASSERT(count == 1, "all faces one island at 120deg");

    mesh_uv_island_set_destroy(&islands);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: smart unwrap produces valid UVs for box                       */
/* ------------------------------------------------------------------ */

static void test_smart_unwrap_box(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    bool ok = mesh_uv_smart_unwrap(&slot, (float)(45.0 * M_PI / 180.0), 0.5f);
    ASSERT(ok, "smart unwrap succeeded");

    /* All UV coords should be finite and within roughly [0,1] range */
    for (uint32_t v = 0; v < slot.vertex_count; v++) {
        float u = slot.uvs[0][v * 2 + 0];
        float w = slot.uvs[0][v * 2 + 1];
        ASSERT(isfinite(u) && isfinite(w), "UV is finite");
        /* Relaxed bounds: islands might pack slightly outside exact [0,1] */
        ASSERT(u >= -0.1f && u <= 1.1f, "U in bounds");
        ASSERT(w >= -0.1f && w <= 1.1f, "V in bounds");
    }

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_uv_smart_unwrap(NULL, 0.5f, 0.5f), "null slot");
    ASSERT(mesh_uv_find_islands(NULL, NULL, 0.5f) == 0, "null find islands");
    g_pass++;
}

int main(void) {
    printf("mesh_uv_smart_tests:\n");
    test_box_islands();
    test_threshold_sensitivity();
    test_smart_unwrap_box();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
