/**
 * @file procgen_wall_diag.c
 * @brief Diagnostic: use actual DPO token file, check all corridor-room junctions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/procgen/procgen_svo_builder.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/procgen_grammar_registry.h"
#include "ferrum/procgen/grammar_blockout.h"

static int svo_solid(const npc_svo_grid_t *g, int x, int y, int z) {
    uint32_t cells = 1u << g->max_depth;
    if (x<0||y<0||z<0||(uint32_t)x>=cells||(uint32_t)y>=cells||(uint32_t)z>=cells) return 0;
    uint32_t n=0, c=cells;
    for(uint32_t d=0;d<g->max_depth;d++){c>>=1;
        uint32_t ci=(((uint32_t)z/c)&1)<<2|(((uint32_t)y/c)&1)<<1|(((uint32_t)x/c)&1);
        uint32_t ch=g->nodes[n].children[ci];if(ch==NPC_SVO_INVALID_NODE)return 0;n=ch;
        if(g->nodes[n].flags&NPC_SVO_FLAG_SOLID)return 1;}
    return 0;
}

int main(int argc, char **argv) {
    const char *fname = (argc > 1) ? argv[1] : "datasets/dpo_txt/001_s42.txt";
    FILE *f = fopen(fname, "r");
    if (!f) { fprintf(stderr, "can't open %s\n", fname); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char *token = malloc(sz + 1); fread(token, 1, sz, f); token[sz] = 0; fclose(f);

    procgen_token_t tokens[4096]; char err[1024]; uint32_t tc = 0;
    procgen_tokenize(token, tokens, 4096, &tc, err, sizeof(err));
    procgen_grammar_registry_init();
    procgen_grammar_t gram = {"blockout",1,procgen_tokenize,grammar_blockout_rasterize,NULL,NULL,0};
    procgen_grammar_register(&gram);
    fr_dungeon_layout_t lay;
    procgen_rasterize_with_registry(tokens, tc, &lay, err, sizeof(err));

    procgen_raster_config_t cfg;
    procgen_raster_config_default(&cfg);
    cfg.max_depth = 7; cfg.world_extent = 64.0f;
    npc_svo_grid_t svo;
    uint32_t solid = procgen_svo_build_cfg(&cfg, &lay, &svo);
    printf("SVO: %u solids\n", solid);
    printf("Layout: %u rooms, %u corridors\n", lay.room_count, lay.corridor_count);

    /* For each corridor, compute its bounding box and check floor +
       check that walls at endpoints aren't blocking passage. */
    for (uint32_t ci = 0; ci < lay.corridor_count; ci++) {
        const fr_corridor_def_t *c = &lay.corridors[ci];

        float hw = c->width * 0.5f;
        float dx = c->to.x - c->from.x;
        float dy = c->to.y - c->from.y;
        float len = sqrtf(dx * dx + dy * dy);
        float perp_x = (len > 0.001f) ? -dy / len * hw : hw;
        float perp_y = (len > 0.001f) ?  dx / len * hw : 0.0f;
        float bound_min_x = fminf(c->from.x, c->to.x) - fabsf(perp_x) - hw;
        float bound_max_x = fmaxf(c->from.x, c->to.x) + fabsf(perp_x) + hw;
        float bound_min_z = fminf(c->from.y, c->to.y) - fabsf(perp_y) - hw;
        float bound_max_z = fmaxf(c->from.y, c->to.y) + fabsf(perp_y) + hw;

        float span = svo.world_bounds.max.x - svo.world_bounds.min.x;
        uint32_t cells = 1u << svo.max_depth;
        int vx_min = (int)((bound_min_x - svo.world_bounds.min.x) / span * cells);
        int vx_max = (int)((bound_max_x - svo.world_bounds.min.x) / span * cells);
        int vy_min = (int)((c->floor_z - svo.world_bounds.min.y) / span * cells);
        int vy_max = (int)((c->ceil_z  - svo.world_bounds.min.y) / span * cells);
        int vz_min = (int)((bound_min_z - svo.world_bounds.min.z) / span * cells);
        int vz_max = (int)((bound_max_z - svo.world_bounds.min.z) / span * cells);

        int horizontal = (fabsf(dx) >= fabsf(dy));

        /* Count missing floor inside the corridor. */
        int floor_gaps = 0;
        for (int vz = vz_min + 1; vz < vz_max; vz++) {
            for (int vx = vx_min + 1; vx < vx_max; vx++) {
                if (!svo_solid(&svo, vx, vy_min, vz)) {
                    if (floor_gaps < 5)
                        printf("  GAP c%u floor at (%d,%d,%d)\n", ci, vx, vy_min, vz);
                    floor_gaps++;
                }
            }
        }
        /* Count blocked passage: check if the cross-section at
           each endpoint (one voxel in) has solid blocks that would
           prevent walking through.  Floor (y=vy_min) and ceiling
           (y=vy_max) are allowed to be solid. */
        int blocked = 0;
        int solid_at_passage = 0;
        if (horizontal) {
            int vx_left = vx_min + 1;
            int vx_right = vx_max - 1;
            for (int y = vy_min + 1; y < vy_max; y++) {
                for (int vz = vz_min + 1; vz < vz_max; vz++) {
                    if (svo_solid(&svo, vx_left,  y, vz)) { solid_at_passage++;
                        if (solid_at_passage <= 8) printf("  BLK%u at (%d,%d,%d) L  world=(%.1f,%.1f,%.1f)\n", ci, vx_left, y, vz,
                            svo.world_bounds.min.x + (vx_left + 0.5f) * svo.voxel_size,
                            svo.world_bounds.min.y + (y + 0.5f) * svo.voxel_size,
                            svo.world_bounds.min.z + (vz + 0.5f) * svo.voxel_size);
                    }
                    blocked++;
                    if (svo_solid(&svo, vx_right, y, vz)) { solid_at_passage++;
                        if (solid_at_passage <= 8) printf("  BLK%u at (%d,%d,%d) R  world=(%.1f,%.1f,%.1f)\n", ci, vx_right, y, vz,
                            svo.world_bounds.min.x + (vx_right + 0.5f) * svo.voxel_size,
                            svo.world_bounds.min.y + (y + 0.5f) * svo.voxel_size,
                            svo.world_bounds.min.z + (vz + 0.5f) * svo.voxel_size);
                    }
                    blocked++;
                }
            }
        } else {
            int vz_bottom = vz_min + 1;
            int vz_top    = vz_max - 1;
            for (int y = vy_min + 1; y < vy_max; y++) {
                for (int vx = vx_min + 1; vx < vx_max; vx++) {
                    if (svo_solid(&svo, vx, y, vz_bottom)) { solid_at_passage++;
                        if (solid_at_passage <= 8) printf("  BLK%u at (%d,%d,%d) B  world=(%.1f,%.1f,%.1f)\n", ci, vx, y, vz_bottom,
                            svo.world_bounds.min.x + (vx + 0.5f) * svo.voxel_size,
                            svo.world_bounds.min.y + (y + 0.5f) * svo.voxel_size,
                            svo.world_bounds.min.z + (vz_bottom + 0.5f) * svo.voxel_size);
                    }
                    blocked++;
                    if (svo_solid(&svo, vx, y, vz_top))    { solid_at_passage++;
                        if (solid_at_passage <= 8) printf("  BLK%u at (%d,%d,%d) T  world=(%.1f,%.1f,%.1f)\n", ci, vx, y, vz_top,
                            svo.world_bounds.min.x + (vx + 0.5f) * svo.voxel_size,
                            svo.world_bounds.min.y + (y + 0.5f) * svo.voxel_size,
                            svo.world_bounds.min.z + (vz_top + 0.5f) * svo.voxel_size);
                    }
                    blocked++;
                }
            }
        }

        printf("Corridor %u: (%.0f,%.0f)→(%.0f,%.0f) w=%.0f %s\n",
               ci, c->from.x, c->from.y, c->to.x, c->to.y, c->width,
               horizontal ? "H" : "V");
        printf("  bounds: X=%d..%d  Z=%d..%d  floor_gaps=%d  passage_solid=%d/%d\n",
               vx_min, vx_max, vz_min, vz_max, floor_gaps, solid_at_passage, blocked);
    }

    free(token);
    npc_svo_grid_destroy(&svo);
    free(lay.rooms);free(lay.corridors);free(lay.openings);free(lay.ramps);free(lay.markers);
    free(lay.nav_nodes);free(lay.nav_edges);
    return 0;
}
