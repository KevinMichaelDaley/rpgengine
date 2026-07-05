/**
 * @file procgen_svo_tests.c
 * @brief Tests for procgen SVO builder and mesh generation.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/procgen/procgen_svo_builder.h"
#include "ferrum/procgen/procgen_layout.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define PASS() g_pass++

static procgen_raster_config_t small_cfg(void) {
    procgen_raster_config_t c;
    procgen_raster_config_default(&c);
    c.world_extent = 32.0f;   /* 64m total */
    c.max_depth    = 8;       /* 256 cells, 0.25m voxels at 64m span */
    return c;
}

static void test_room_rasterizes(void) {
    procgen_raster_config_t cfg = small_cfg();

    fr_dungeon_layout_t layout;
    memset(&layout, 0, sizeof(layout));
    fr_room_def_t *r = calloc(1, sizeof(fr_room_def_t));
    ASSERT_TRUE(r != NULL);
    r->vertex_count = 4;
    r->vertices[0] = (vec3_t){0.0f, 0.0f, 0.0f};
    r->vertices[1] = (vec3_t){8.0f, 0.0f, 0.0f};
    r->vertices[2] = (vec3_t){8.0f, 8.0f, 0.0f};
    r->vertices[3] = (vec3_t){0.0f, 8.0f, 0.0f};
    r->floor_z = 0.0f;
    r->ceil_z  = 3.0f;
    layout.rooms = r;
    layout.room_count = 1;

    npc_svo_grid_t grid;
    uint32_t marked = procgen_svo_build_cfg(&cfg, &layout, &grid);
    ASSERT_TRUE(marked > 0);
    ASSERT_TRUE(marked >= 1); /* coarse blocks (8-voxel), 8m room → ~64 blocks */
    ASSERT_TRUE(grid.node_count > 0);

    npc_svo_grid_destroy(&grid);
    free(r);
    PASS();
}

static void test_corridor_rasterizes(void) {
    procgen_raster_config_t cfg = small_cfg();

    fr_dungeon_layout_t layout;
    memset(&layout, 0, sizeof(layout));
    fr_corridor_def_t *c = calloc(1, sizeof(fr_corridor_def_t));
    ASSERT_TRUE(c != NULL);
    c->from    = (vec3_t){0.0f, 4.0f, 0.0f};
    c->to      = (vec3_t){16.0f, 4.0f, 0.0f};
    c->width   = 2.0f;
    c->floor_z = 0.0f;
    c->ceil_z  = 3.0f;
    layout.corridors = c;
    layout.corridor_count = 1;

    npc_svo_grid_t grid;
    uint32_t marked = procgen_svo_build_cfg(&cfg, &layout, &grid);
    ASSERT_TRUE(marked > 0);
    ASSERT_TRUE(grid.node_count > 0);

    npc_svo_grid_destroy(&grid);
    free(c);
    PASS();
}

static void test_mesh_generation(void) {
    procgen_raster_config_t cfg = small_cfg();

    fr_dungeon_layout_t layout;
    memset(&layout, 0, sizeof(layout));
    fr_room_def_t *r = calloc(1, sizeof(fr_room_def_t));
    ASSERT_TRUE(r != NULL);
    r->vertex_count = 4;
    r->vertices[0] = (vec3_t){0,0,0};
    r->vertices[1] = (vec3_t){4,0,0};
    r->vertices[2] = (vec3_t){4,4,0};
    r->vertices[3] = (vec3_t){0,4,0};
    r->floor_z = 0; r->ceil_z = 2;
    layout.rooms = r;
    layout.room_count = 1;

    npc_svo_grid_t grid;
    uint32_t marked = procgen_svo_build_cfg(&cfg, &layout, &grid);
    ASSERT_TRUE(marked > 0);

    procgen_mesh_t mesh;
    procgen_mesh_init(&mesh);
    uint32_t tris = procgen_mesh_from_svo(&grid, &mesh);
    ASSERT_TRUE(tris > 0);
    ASSERT_TRUE(mesh.vertex_count > 0);

    /* Verify vertices are finite */
    for (uint32_t i = 0; i < mesh.vertex_count; i++) {
        ASSERT_TRUE(isfinite(mesh.vertices[i]));
    }

    procgen_mesh_destroy(&mesh);
    npc_svo_grid_destroy(&grid);
    free(r);
    PASS();
}

static void test_empty_layout(void) {
    procgen_raster_config_t cfg = small_cfg();
    fr_dungeon_layout_t layout;
    memset(&layout, 0, sizeof(layout));
    npc_svo_grid_t grid;
    uint32_t marked = procgen_svo_build_cfg(&cfg, &layout, &grid);
    ASSERT_INT_EQ(marked, (uint32_t)0);
    npc_svo_grid_destroy(&grid);
    PASS();
}

int main(void) {
    printf("=== Procgen SVO Tests ===\n\n");
    RUN(test_room_rasterizes);
    RUN(test_corridor_rasterizes);
    RUN(test_mesh_generation);
    RUN(test_empty_layout);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
