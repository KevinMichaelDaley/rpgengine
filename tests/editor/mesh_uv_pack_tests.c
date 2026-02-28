/**
 * @file mesh_uv_pack_tests.c
 * @brief Tests for UV island packing and texel density.
 */
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "ferrum/editor/mesh/mesh_uv_pack.h"
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
/* Test: pack single island stays in [0,1]                             */
/* ------------------------------------------------------------------ */

static void test_pack_single(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    /* Put all in one island (high threshold) */
    mesh_uv_island_set_t islands;
    mesh_uv_island_set_init(&islands);
    mesh_uv_find_islands(&slot, &islands, (float)(120.0 * M_PI / 180.0));

    /* First flatten so UVs exist */
    mesh_uv_smart_unwrap(&slot, (float)(120.0 * M_PI / 180.0), 0.5f);

    /* Pack with no padding */
    bool ok = mesh_uv_pack_islands(&slot, &islands, 0.0f, 512);
    ASSERT(ok, "pack succeeded");

    /* All UVs should be within [0,1] */
    for (uint32_t v = 0; v < slot.vertex_count; v++) {
        float u = slot.uvs[0][v * 2 + 0];
        float w = slot.uvs[0][v * 2 + 1];
        ASSERT(u >= -0.01f && u <= 1.01f, "U in [0,1]");
        ASSERT(w >= -0.01f && w <= 1.01f, "V in [0,1]");
    }

    mesh_uv_island_set_destroy(&islands);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: pack multiple islands                                         */
/* ------------------------------------------------------------------ */

static void test_pack_multi(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    /* 6 islands at 45deg */
    mesh_uv_island_set_t islands;
    mesh_uv_island_set_init(&islands);
    mesh_uv_find_islands(&slot, &islands, (float)(45.0 * M_PI / 180.0));
    ASSERT(islands.count == 6, "got 6 islands");

    /* Flatten first */
    mesh_uv_smart_unwrap(&slot, (float)(45.0 * M_PI / 180.0), 0.5f);

    /* Re-detect islands after unwrap */
    mesh_uv_island_set_destroy(&islands);
    mesh_uv_island_set_init(&islands);
    mesh_uv_find_islands(&slot, &islands, (float)(45.0 * M_PI / 180.0));

    /* Pack */
    bool ok = mesh_uv_pack_islands(&slot, &islands, 0.01f, 512);
    ASSERT(ok, "pack succeeded");

    /* All UVs in [0,1] */
    for (uint32_t v = 0; v < slot.vertex_count; v++) {
        float u = slot.uvs[0][v * 2 + 0];
        float w = slot.uvs[0][v * 2 + 1];
        ASSERT(u >= -0.01f && u <= 1.01f, "U in [0,1]");
        ASSERT(w >= -0.01f && w <= 1.01f, "V in [0,1]");
    }

    mesh_uv_island_set_destroy(&islands);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: texel density calculation                                     */
/* ------------------------------------------------------------------ */

static void test_texel_density(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});
    mesh_uv_smart_unwrap(&slot, (float)(120.0 * M_PI / 180.0), 0.5f);

    float density = mesh_uv_texel_density(&slot, 512);
    ASSERT(density > 0.0f, "density positive");
    ASSERT(isfinite(density), "density finite");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_uv_pack_islands(NULL, NULL, 0, 512), "null pack");
    ASSERT(mesh_uv_texel_density(NULL, 512) == 0.0f, "null density");
    g_pass++;
}

int main(void) {
    printf("mesh_uv_pack_tests:\n");
    test_pack_single();
    test_pack_multi();
    test_texel_density();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
