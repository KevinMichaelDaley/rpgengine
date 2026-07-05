/**
 * @file procgen_debug_tests.c
 * @brief DEBUG: generate simple scenes, export SVO occupancy + mesh OBJs.
 *
 * Usage: ./build/procgen_debug_tests
 *
 * Produces:
 *   /tmp/debug_room.txt       — grammar token string
 *   /tmp/debug_room_svo.txt   — ASCII slice of SVO occupancy
 *   /tmp/debug_room.obj       — mesh OBJ
 *   /tmp/debug_corridor.txt   — grammar token string
 *   /tmp/debug_corridor_svo.txt
 *   /tmp/debug_corridor.obj
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/procgen/procgen_svo_builder.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/procgen_grammar_registry.h"
#include "ferrum/procgen/grammar_blockout.h"

/* ── Helpers ─────────────────────────────────────────────────── */

static void export_obj(const procgen_mesh_t *mesh, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    fprintf(f, "# %u verts\n", mesh->vertex_count / 3);
    for (uint32_t i = 0; i < mesh->vertex_count; i += 3)
        fprintf(f, "v %.3f %.3f %.3f\n",
                mesh->vertices[i], mesh->vertices[i+1], mesh->vertices[i+2]);
    for (uint32_t i = 0; i < mesh->vertex_count; i += 9)
        fprintf(f, "f %u %u %u\n", i/3+1, i/3+2, i/3+3);
    fclose(f);
    printf("  OBJ: %s (%u tris)\n", path, mesh->vertex_count / 9);
}

static int is_solid(const npc_svo_grid_t *g, int x, int y, int z) {
    uint32_t cells = 1u << g->max_depth;
    if (x < 0 || y < 0 || z < 0 || (uint32_t)x >= cells || (uint32_t)y >= cells || (uint32_t)z >= cells)
        return 0;
    uint32_t n = 0, c = cells;
    for (uint32_t d = 0; d < g->max_depth; d++) {
        c >>= 1;
        uint32_t ci = (((uint32_t)z / c) & 1) << 2 | (((uint32_t)y / c) & 1) << 1 | (((uint32_t)x / c) & 1);
        uint32_t ch = g->nodes[n].children[ci];
        if (ch == NPC_SVO_INVALID_NODE) return 0;
        n = ch;
        if (g->nodes[n].flags & NPC_SVO_FLAG_SOLID) return 1;
    }
    return 0;
}

static void export_svo_slice(const npc_svo_grid_t *g, const char *path) {
    uint32_t cells = 1u << g->max_depth;
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# SVO occupancy: %u cells per axis\n", cells);
    /* Print 3 cross-sections at center */
    uint32_t mid = cells / 2;
    char row[1024];

    fprintf(f, "\n== XZ slice at Y=%u ==\n", mid);
    for (uint32_t z = 0; z < cells && z < 64; z++) {
        for (uint32_t x = 0; x < cells && x < 64; x++) {
            int s = is_solid(g, (int)x, (int)mid, (int)z);
            row[x] = s ? '#' : '.';
        }
        row[cells < 64 ? cells : 64] = '\0';
        fprintf(f, "%s\n", row);
    }

    fprintf(f, "\n== XY slice at Z=%u ==\n", mid);
    for (uint32_t y = 0; y < cells && y < 64; y++) {
        for (uint32_t x = 0; x < cells && x < 64; x++) {
            int s = is_solid(g, (int)x, (int)y, (int)mid);
            row[x] = s ? '#' : '.';
        }
        row[cells < 64 ? cells : 64] = '\0';
        fprintf(f, "%s\n", row);
    }

    fprintf(f, "\n== YZ slice at X=%u ==\n", mid);
    for (uint32_t z = 0; z < cells && z < 64; z++) {
        for (uint32_t y = 0; y < cells && y < 64; y++) {
            int s = is_solid(g, (int)mid, (int)y, (int)z);
            row[y] = s ? '#' : '.';
        }
        row[cells < 64 ? cells : 64] = '\0';
        fprintf(f, "%s\n", row);
    }
    fclose(f);
    printf("  SVO:  %s\n", path);
}

static int run_pipeline(const char *token, const char *prefix) {
    char path_txt[256], path_svo[256], path_obj[256];
    snprintf(path_txt, sizeof(path_txt), "/tmp/%s.txt", prefix);
    snprintf(path_svo, sizeof(path_svo), "/tmp/%s_svo.txt", prefix);
    snprintf(path_obj, sizeof(path_obj), "/tmp/%s.obj", prefix);

    printf("=== %s ===\n", prefix);
    FILE *f = fopen(path_txt, "w");
    fputs(token, f); fclose(f);

    procgen_token_t tokens[4096];
    char err[1024];
    uint32_t count = 0;
    if (procgen_tokenize(token, tokens, 4096, &count, err, sizeof(err)) != TOK_ERR_NONE) {
        printf("  TOKENIZE FAILED: %s\n", err);
        return -1;
    }

    procgen_grammar_registry_init();
    procgen_grammar_t g = {
        .name = "blockout", .version = 1,
        .tokenize = procgen_tokenize,
        .rasterize = grammar_blockout_rasterize
    };
    procgen_grammar_register(&g);

    fr_dungeon_layout_t layout;
    if (procgen_rasterize_with_registry(tokens, count, &layout, err, sizeof(err)) != 0) {
        printf("  RASTERIZE FAILED: %s\n", err);
        return -1;
    }
    printf("  Layout: %u rooms, %u corridors\n", layout.room_count, layout.corridor_count);

    procgen_raster_config_t cfg;
    procgen_raster_config_default(&cfg);
    cfg.max_depth = 9;
    cfg.world_extent = 256.0f;

    npc_svo_grid_t svo;
    uint32_t solid = procgen_svo_build_cfg(&cfg, &layout, &svo);
    printf("  SVO: %u solid voxels, %u nodes\n", solid, svo.node_count);
    export_svo_slice(&svo, path_svo);

    procgen_mesh_t mesh;
    procgen_mesh_init(&mesh);
    uint32_t tris = procgen_mesh_from_svo(&svo, &mesh);
    printf("  Mesh: %u triangles\n", tris);
    export_obj(&mesh, path_obj);

    procgen_mesh_destroy(&mesh);
    npc_svo_grid_destroy(&svo);
    free(layout.rooms); free(layout.corridors);
    free(layout.openings); free(layout.ramps); free(layout.markers);
    free(layout.nav_nodes); free(layout.nav_edges);

    return 0;
}

int main(void) {
    /* Scene 1: room with door */
    const char *room_door =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6 name=main\n"
        "SPAWN x=5 y=5 z=1\n"
        "DOOR at=(10,5) w=2 h=3\n"
        "MARKER x=5 y=5 z=1 name=center\n"
        "MARKER x=9 y=5 z=1 name=door\n"
        "MARKER x=1 y=1 z=1 name=corner\n";
    run_pipeline(room_door, "debug_room");

    /* Scene 2: two rooms with corridor */
    const char *room_corr =
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
    run_pipeline(room_corr, "debug_corridor");

    printf("\nDone. Files:\n");
    printf("  /tmp/debug_room.txt /tmp/debug_room_svo.txt /tmp/debug_room.obj\n");
    printf("  /tmp/debug_corridor.txt /tmp/debug_corridor_svo.txt /tmp/debug_corridor.obj\n");
    return 0;
}
