/**
 * @file procgen_chunk_diag.c
 * @brief Diagnostic: verify SVO rasterization produces correct solid voxels.
 *
 * Builds a room via chunk builder, then queries every voxel using the
 * same svo_is_solid function the mesh generator uses.  Reports mismatches.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/procgen/procgen_chunk_builder.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/procgen_grammar_registry.h"
#include "ferrum/procgen/grammar_blockout.h"

/* The exact query used by procgen_mesh_from_svo */
static int svo_query(const npc_svo_grid_t *g, int x, int y, int z) {
    uint32_t cells = 1u << g->max_depth;
    if (x < 0 || y < 0 || z < 0 || (uint32_t)x >= cells ||
        (uint32_t)y >= cells || (uint32_t)z >= cells) return 0;
    uint32_t node = 0;
    uint32_t c    = cells;
    for (uint32_t d = 0; d < g->max_depth; d++) {
        c >>= 1;
        uint32_t cx = ((uint32_t)z / c) & 1;
        uint32_t cy = ((uint32_t)y / c) & 1;
        uint32_t cz2 = ((uint32_t)x / c) & 1;
        uint32_t ci = (cx << 2) | (cy << 1) | cz2;
        uint32_t ch = g->nodes[node].children[ci];
        if (ch == NPC_SVO_INVALID_NODE) return 0;
        node = ch;
        if (g->nodes[node].flags & NPC_SVO_FLAG_SOLID) return 1;
    }
    return 0;
}

int main(void) {
    const char *token =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=3 name=r\n"
        "SPAWN x=2 y=2 z=1\n"
        "MARKER x=2 y=2 z=1 name=c\n";

    procgen_token_t   tokens[4096];
    char              err[1024];
    uint32_t          token_count = 0;

    if (procgen_tokenize(token, tokens, 4096, &token_count,
                         err, sizeof(err)) != TOK_ERR_NONE) {
        fprintf(stderr, "tokenize: %s\n", err);
        return 1;
    }

    procgen_grammar_registry_init();
    procgen_grammar_t grammar = {
        .name      = "blockout",
        .version   = 1,
        .tokenize  = procgen_tokenize,
        .rasterize = grammar_blockout_rasterize,
    };
    procgen_grammar_register(&grammar);

    fr_dungeon_layout_t layout;
    if (procgen_rasterize_with_registry(tokens, token_count,
                                         &layout, err, sizeof(err)) != 0) {
        fprintf(stderr, "rasterize: %s\n", err);
        return 1;
    }
    printf("Layout: %u rooms, %u corridors\n",
           layout.room_count, layout.corridor_count);

    /* Build chunk grid. 16m chunk base, 1m voxels (256 cells). */
    procgen_chunk_grid_t grid;
    procgen_chunk_grid_init(&grid, 16.0f, 8, 32.0f);
    uint32_t built = procgen_chunk_grid_build(&grid, &layout);
    printf("Chunks built: %u\n", built);

    int total_solid  = 0;
    int total_queried = 0;

    for (size_t ci = 0; ci < (size_t)grid.count_x * grid.count_z; ci++) {
        if (!grid.chunks[ci].loaded) continue;
        procgen_chunk_t *c = &grid.chunks[ci];
        uint32_t cells    = 1u << c->max_depth;
        int      solid    = 0;

        printf("\nChunk %zu: depth=%u cells=%u nodes=%u cap=%u\n",
               ci, c->max_depth, cells, c->svo.node_count, c->svo.node_cap);
        printf("  origin (%.0f, %.0f, %.0f)\n",
               c->origin_x, c->origin_y, c->origin_z);

        /* Count solid voxels in the region where the room should be.
           Room at world X=0..5, Y=0..3, Z=0..5.
           Convert to chunk voxel coords. */
        float vs = c->svo.voxel_size;
        int ix0 = (int)((0.0f - c->origin_x) / vs);
        int ix1 = (int)((5.0f - c->origin_x) / vs);
        int iy0 = (int)((0.0f - c->origin_y) / vs);
        int iy1 = (int)((3.0f - c->origin_y) / vs);
        int iz0 = (int)((0.0f - c->origin_z) / vs);
        int iz1 = (int)((5.0f - c->origin_z) / vs);
        printf("  expected room voxel range: X=%d..%d Y=%d..%d Z=%d..%d\n",
               ix0, ix1, iy0, iy1, iz0, iz1);

        for (int z = iz0; z <= iz1; z++) {
            for (int y = iy0; y <= iy1; y++) {
                for (int x = ix0; x <= ix1; x++) {
                    int s = svo_query(&c->svo, x, y, z);
                    if (s) solid++;
                }
            }
        }

        total_solid += solid;
        total_queried += (ix1-ix0+1)*(iy1-iy0+1)*(iz1-iz0+1);
        printf("  solid in room box: %d / %d\n", solid,
               (ix1-ix0+1)*(iy1-iy0+1)*(iz1-iz0+1));

        /* Print a 2D slice at Y=iy0 (floor) to see pattern */
        printf("  XZ slice at floor Y=%d:\n", iy0);
        for (int z = iz0; z <= iz1; z++) {
            printf("    Z=%2d |", z);
            for (int x = ix0; x <= ix1; x++) {
                printf("%c", svo_query(&c->svo, x, iy0, z) ? '#' : '.');
            }
            printf("|\n");
        }
    }

    printf("\nTotal: solid=%d / queried=%d\n", total_solid, total_queried);

    procgen_chunk_grid_destroy(&grid);
    free(layout.rooms);
    free(layout.corridors);
    free(layout.openings);
    free(layout.ramps);
    free(layout.markers);
    free(layout.nav_nodes);
    free(layout.nav_edges);
    return total_solid == 0 ? 1 : 0;
}
