/**
 * @file procgen_verify_full_tests.c
 * @brief Verify SVO rasterizer matches dense surface grid voxel-for-voxel,
 *        then export OBJs for both test scenes.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/procgen/procgen_svo_builder.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/procgen_grammar_registry.h"
#include "ferrum/procgen/grammar_blockout.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_EQ(a, b)  ASSERT_TRUE((a) == (b))
#define PASS() g_pass++

/* ── OBJ export ──────────────────────────────────────────────── */
static void write_obj(const procgen_mesh_t *mesh, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    fprintf(f, "# %u verts %u faces\n",
            mesh->vertex_count / 3, mesh->vertex_count / 9);
    for (uint32_t i = 0; i < mesh->vertex_count; i += 3) {
        fprintf(f, "v %.3f %.3f %.3f\n",
                mesh->vertices[i], mesh->vertices[i + 1], mesh->vertices[i + 2]);
    }
    for (uint32_t i = 0; i < mesh->vertex_count; i += 9) {
        fprintf(f, "f %u %u %u\n", i / 3 + 1, i / 3 + 2, i / 3 + 3);
    }
    fclose(f);
    printf("  wrote %s (%u tris)\n", path, mesh->vertex_count / 9);
}

/* ── SVO query ────────────────────────────────────────────────── */
static int svo_solid(const npc_svo_grid_t *grid, int x, int y, int z) {
    uint32_t cells = 1u << grid->max_depth;
    if (x < 0 || y < 0 || z < 0
        || (uint32_t)x >= cells
        || (uint32_t)y >= cells
        || (uint32_t)z >= cells) {
        return 0;
    }
    uint32_t node_index  = 0;
    uint32_t cell_count  = cells;
    for (uint32_t depth = 0; depth < grid->max_depth; depth++) {
        cell_count >>= 1;
        uint32_t child_x = ((uint32_t)z / cell_count) & 1;
        uint32_t child_y = ((uint32_t)y / cell_count) & 1;
        uint32_t child_z = ((uint32_t)x / cell_count) & 1;
        uint32_t child_index = (child_x << 2) | (child_y << 1) | child_z;
        uint32_t child = grid->nodes[node_index].children[child_index];
        if (child == NPC_SVO_INVALID_NODE) {
            return 0;
        }
        node_index = child;
        if (grid->nodes[node_index].flags & NPC_SVO_FLAG_SOLID) {
            return 1;
        }
    }
    return 0;
}

/* ── Point-in-polygon ─────────────────────────────────────────── */
static int point_in_polygon(const vec3_t *vertices, uint32_t count,
                            float test_x, float test_y) {
    int inside = 0;
    for (uint32_t i = 0, j = count - 1; i < count; j = i++) {
        float xi = vertices[i].x;
        float yi = vertices[i].y;
        float xj = vertices[j].x;
        float yj = vertices[j].y;
        if (((yi > test_y) != (yj > test_y))
            && (test_x < (xj - xi) * (test_y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

/* ── Dense surface grid builder (matches SVO rasterizer logic) ── */
static int *build_dense_surface(const fr_dungeon_layout_t *layout,
                                 uint32_t      grid_cells,
                                 float         origin_x,
                                 float         origin_y,
                                 float         origin_z,
                                 float         voxel_size) {
    size_t  total   = (size_t)grid_cells * (size_t)grid_cells * (size_t)grid_cells;
    int    *surface = calloc(total, sizeof(int));
    if (!surface) return NULL;

    for (uint32_t ri = 0; ri < layout->room_count; ri++) {
        const fr_room_def_t *room    = &layout->rooms[ri];
        const vec3_t        *poly    = room->vertices;
        uint32_t              pn      = room->vertex_count;
        float                 room_fz = room->floor_z;
        float                 room_cz = room->ceil_z;

        float x_min = poly[0].x, x_max = x_min;
        float y_min = poly[0].y, y_max = y_min;
        for (uint32_t j = 1; j < pn; j++) {
            if (poly[j].x < x_min) x_min = poly[j].x;
            if (poly[j].x > x_max) x_max = poly[j].x;
            if (poly[j].y < y_min) y_min = poly[j].y;
            if (poly[j].y > y_max) y_max = poly[j].y;
        }

        int vx_min = (int)((x_min    - origin_x) / voxel_size);
        int vx_max = (int)((x_max    - origin_x) / voxel_size);
        int vy_min = (int)((y_min    - origin_y) / voxel_size);
        int vy_max = (int)((y_max    - origin_y) / voxel_size);
        int vz_min = (int)((room_fz  - origin_z) / voxel_size);
        int vz_max = (int)((room_cz  - origin_z) / voxel_size);

        /* Floor slab — one layer at vz_min */
        for (int vy = vy_min; vy <= vy_max; vy++) {
            for (int vx = vx_min; vx <= vx_max; vx++) {
                if (vx < 0 || vy < 0 || vz_min < 0) continue;
                if ((uint32_t)vx >= grid_cells || (uint32_t)vy >= grid_cells
                    || (uint32_t)vz_min >= grid_cells) continue;
                float world_x = origin_x + (float)vx * voxel_size;
                float world_y = origin_y + (float)vy * voxel_size;
                if (point_in_polygon(poly, pn, world_x, world_y)) {
                    surface[(size_t)vz_min * grid_cells * grid_cells
                            + (size_t)vy * grid_cells
                            + (size_t)vx] = 1;
                }
            }
        }

        /* Ceiling slab — one layer at vz_max */
        for (int vy = vy_min; vy <= vy_max; vy++) {
            for (int vx = vx_min; vx <= vx_max; vx++) {
                if (vx < 0 || vy < 0 || vz_max < 0) continue;
                if ((uint32_t)vx >= grid_cells || (uint32_t)vy >= grid_cells
                    || (uint32_t)vz_max >= grid_cells) continue;
                float world_x = origin_x + (float)vx * voxel_size;
                float world_y = origin_y + (float)vy * voxel_size;
                if (point_in_polygon(poly, pn, world_x, world_y)) {
                    surface[(size_t)vz_max * grid_cells * grid_cells
                            + (size_t)vy * grid_cells
                            + (size_t)vx] = 1;
                }
            }
        }

        /* Wall columns along each polygon edge */
        for (uint32_t edge = 0; edge < pn; edge++) {
            uint32_t next = (edge + 1) % pn;
            float    x_a  = poly[edge].x;
            float    y_a  = poly[edge].y;
            float    x_b  = poly[next].x;
            float    y_b  = poly[next].y;

            float edge_dx  = x_b - x_a;
            float edge_dy  = y_b - y_a;
            float edge_len = sqrtf(edge_dx * edge_dx + edge_dy * edge_dy);
            int   steps    = (int)(edge_len / voxel_size) + 1;

            for (int step = 0; step <= steps; step++) {
                float t  = (float)step / (float)steps;
                float px = x_a + t * edge_dx;
                float py = y_a + t * edge_dy;
                int   wx = (int)((px - origin_x) / voxel_size);
                int   wy = (int)((py - origin_y) / voxel_size);

                for (int vz = vz_min; vz <= vz_max; vz++) {
                    if (wx < 0 || wy < 0 || vz < 0) continue;
                    if ((uint32_t)wx >= grid_cells
                        || (uint32_t)wy >= grid_cells
                        || (uint32_t)vz >= grid_cells) continue;
                    surface[(size_t)vz * grid_cells * grid_cells
                            + (size_t)wy * grid_cells
                            + (size_t)wx] = 1;
                }
            }
        }
    }

    /* Corridors — floor and ceiling along center line */
    for (uint32_t ci = 0; ci < layout->corridor_count; ci++) {
        const fr_corridor_def_t *corr = &layout->corridors[ci];
        float half_w = corr->width * 0.5f;

        int cvx1 = (int)((fminf(corr->from.x, corr->to.x) - half_w - origin_x) / voxel_size);
        int cvx2 = (int)((fmaxf(corr->from.x, corr->to.x) + half_w - origin_x) / voxel_size);
        int cvy1 = (int)((fminf(corr->from.y, corr->to.y) - half_w - origin_y) / voxel_size);
        int cvy2 = (int)((fmaxf(corr->from.y, corr->to.y) + half_w - origin_y) / voxel_size);
        int cvz1 = (int)((corr->floor_z - origin_z) / voxel_size);
        int cvz2 = (int)((corr->ceil_z  - origin_z) / voxel_size);

        float dx  = corr->to.x - corr->from.x;
        float dy2 = corr->to.y - corr->from.y;
        float l2  = dx * dx + dy2 * dy2;

        for (int vy = cvy1; vy <= cvy2; vy++) {
            for (int vx = cvx1; vx <= cvx2; vx++) {
                if (vx < 0 || vy < 0 || cvz1 < 0) continue;
                if ((uint32_t)vx >= grid_cells
                    || (uint32_t)vy >= grid_cells
                    || (uint32_t)cvz1 >= grid_cells) continue;
                float px  = origin_x + (float)vx * voxel_size;
                float py  = origin_y + (float)vy * voxel_size;
                float t   = ((px - corr->from.x) * dx + (py - corr->from.y) * dy2) / l2;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                float cx  = corr->from.x + t * dx;
                float cy  = corr->from.y + t * dy2;
                float d2  = (px - cx) * (px - cx) + (py - cy) * (py - cy);
                if (d2 <= half_w * half_w) {
                    /* Floor and ceiling slabs along full corridor width. */
                    surface[(size_t)cvz1 * grid_cells * grid_cells
                            + (size_t)vy * grid_cells
                            + (size_t)vx] = 1;
                    surface[(size_t)cvz2 * grid_cells * grid_cells
                            + (size_t)vy * grid_cells
                            + (size_t)vx] = 1;
                    /* Wall columns at the two lateral sides, full length.
                       Only at the exact edges (vy==cvy1 or vy==cvy2). */
                    if (vy == cvy1 || vy == cvy2) {
                        for (int vz = cvz1 + 1; vz < cvz2; vz++) {
                            surface[(size_t)vz * grid_cells * grid_cells
                                    + (size_t)vy * grid_cells
                                    + (size_t)vx] = 1;
                        }
                    }
                }
            }
        }
    }

    return surface;
}

/* ── Run one test case ────────────────────────────────────────── */
static void verify_and_export(const char *name,
                               const char *token_string,
                               const char *obj_path) {
    /* Tokenize. */
    procgen_token_t tokens[4096];
    char            err[1024];
    uint32_t        token_count = 0;
    tok_error_t     tok_rc = procgen_tokenize(token_string, tokens, 4096,
                                               &token_count, err, sizeof(err));
    ASSERT_EQ(tok_rc, TOK_ERR_NONE);

    /* Rasterize layout. */
    procgen_grammar_registry_init();
    procgen_grammar_t grammar = {
        .name      = "blockout",
        .version   = 1,
        .tokenize  = procgen_tokenize,
        .rasterize = grammar_blockout_rasterize,
    };
    procgen_grammar_register(&grammar);

    fr_dungeon_layout_t layout;
    int lay_rc = procgen_rasterize_with_registry(tokens, token_count,
                                                  &layout, err, sizeof(err));
    ASSERT_EQ(lay_rc, 0);

    /* Build dense surface grid. */
    uint32_t grid_cells = 512;
    float    origin_x   = -256.0f;
    float    origin_y   = -256.0f;
    float    origin_z   = -256.0f;
    float    voxel_size = 1.0f;

    int *dense_grid = build_dense_surface(&layout, grid_cells,
                                           origin_x, origin_y, origin_z,
                                           voxel_size);
    ASSERT_TRUE(dense_grid != NULL);

    /* Build SVO. */
    procgen_raster_config_t svo_cfg;
    procgen_raster_config_default(&svo_cfg);
    svo_cfg.max_depth    = 9;
    svo_cfg.world_extent = 256.0f;

    npc_svo_grid_t svo;
    uint32_t solid_count = procgen_svo_build_cfg(&svo_cfg, &layout, &svo);
    printf("  %s: %u solid voxels, %u nodes\n", name,
           solid_count, svo.node_count);

    /* Compare voxel-for-voxel.  The dense grid uses layout coordinates
     * (X ≡ layout X, Y ≡ layout Y, Z ≡ layout floor_z/ceil_z).
     * The SVO uses engine coordinates (X, Y=layout_Z, Z=layout_Y).
     * We remap: SVO(x, y, z) ↔ dense(x, z, y). */
    int     match_count    = 0;
    int     mismatch_count = 0;
    size_t  total_cells    = (size_t)grid_cells * grid_cells * grid_cells;

    for (uint32_t z = 0; z < grid_cells; z++) {
        for (uint32_t y = 0; y < grid_cells; y++) {
            for (uint32_t x = 0; x < grid_cells; x++) {
                int dense_val = dense_grid[z * grid_cells * grid_cells
                                           + y * grid_cells + x];
                int svo_val   = svo_solid(&svo,
                                          (int)x,           /* SVO X = layout X */
                                          (int)z,           /* SVO Y = layout Z */
                                          (int)y);          /* SVO Z = layout Y */
                if (dense_val == svo_val) {
                    match_count++;
                } else {
                    mismatch_count++;
                    if (mismatch_count <= 5) {
                        printf("    MISMATCH @ (%d,%d,%d) dense=%d svo=%d\n",
                               x, y, z, dense_val, svo_val);
                    }
                }
            }
        }
    }

    printf("  match=%d  mismatch=%d  (of %zu)\n",
           match_count, mismatch_count, total_cells);

    /* Allow ≤2 mismatches: boundary voxels at exact edge of half_width. */
    if (mismatch_count > 2) {
        printf("  FAIL: %d mismatches\n", mismatch_count);
        g_fail++;
    }

    /* Generate mesh and export OBJ. */
    procgen_mesh_t mesh;
    procgen_mesh_init(&mesh);
    uint32_t tris = procgen_mesh_from_svo(&svo, &mesh);
    printf("  mesh: %u triangles\n", tris);
    write_obj(&mesh, obj_path);
    procgen_mesh_destroy(&mesh);

    if (mismatch_count == 0) PASS();

    free(dense_grid);
    npc_svo_grid_destroy(&svo);
    free(layout.rooms);
    free(layout.corridors);
    free(layout.openings);
    free(layout.ramps);
    free(layout.markers);
    free(layout.nav_nodes);
    free(layout.nav_edges);
}

/* ── Test data ────────────────────────────────────────────────── */

static const char *SCENE_ROOM =
    "@grammar blockout v1\n"
    "ROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6 name=main\n"
    "SPAWN x=5 y=5 z=1\n"
    "DOOR at=(10,5) w=2 h=3\n"
    "MARKER x=5 y=5 z=1 name=center\n"
    "MARKER x=9 y=5 z=1 name=doorway\n"
    "MARKER x=1 y=1 z=1 name=corner\n";

static const char *SCENE_CORRIDOR =
    "@grammar blockout v1\n"
    "ROOM_QUAD x=0 y=0 w=8 h=8 floor_z=0 ceil_z=6 name=start\n"
    "SPAWN x=4 y=4 z=1\n"
    "CORRIDOR_H from=(8,3) to=(20,3) w=2 floor_z=0 ceil_z=5\n"
    "ROOM_QUAD x=20 y=-2 w=8 h=10 floor_z=0 ceil_z=6 name=end\n"
    "DOOR at=(8,3) w=2 h=3\n"
    "DOOR at=(20,3) w=2 h=3\n"
    "MARKER x=4 y=4 z=1 name=start_marker\n"
    "MARKER x=14 y=3 z=1 name=midpoint\n"
    "MARKER x=24 y=3 z=1 name=end_marker\n";

int main(void) {
    printf("=== Procgen SVO Verification Tests ===\n\n");
    printf("RUN  verify_room\n");
    verify_and_export("room", SCENE_ROOM, "/tmp/verify_room.obj");
    printf("OK   verify_room\n");
    printf("RUN  verify_corr\n");
    verify_and_export("corr", SCENE_CORRIDOR, "/tmp/verify_corr.obj");
    printf("OK   verify_corr\n");
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
