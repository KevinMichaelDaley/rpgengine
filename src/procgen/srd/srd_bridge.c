/**
 * @file srd_bridge.c
 * @brief SRD bridge: ASCII floor plan → SDF grid → optimize → SVO.
 *
 * Non-static functions (3): srd_generate_svo, srd_generate, srd_free_geometry
 */
#include "ferrum/procgen/srd/srd_bridge.h"
#include "ferrum/procgen/srd/srd_grammar.h"
#include "ferrum/procgen/srd/srd_seed_init.h"
#include "ferrum/procgen/srd/srd_descent_loop.h"
#include "ferrum/procgen/srd/srd_descent_config.h"
#include "ferrum/procgen/srd/srd_voxel_rule_table.h"
#include "ferrum/procgen/srd/srd_sdf_to_svo.h"
#include "ferrum/procgen/srd/srd_room_type.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── ASCII parsing (reused from old bridge) ──────────────────── */

/**
 * @brief Parse ASCII string into lines, stopping at LOSS: block or end.
 *
 * Splits the ASCII string on newlines, skipping === header lines.
 * Returns the number of lines parsed (up to max_lines).
 */
static int parse_ascii_lines(const char *ascii,
                             char line_buf[][128], const char **lines,
                             int max_lines) {
    const char *p = ascii;
    int nl = 0;

    while (*p && nl < max_lines) {
        /* Skip blank lines */
        while (*p == '\n' || *p == '\r')
            p++;
        if (!*p) break;

        /* Skip === header lines */
        if (*p == '=' && p[1] == '=' && p[2] == '=') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        /* Stop at LOSS: block (legacy, no longer used) */
        if (*p == 'L' && !strncmp(p, "LOSS:", 5))
            break;

        /* Extract line */
        const char *s = p;
        while (*p && *p != '\n' && *p != '\r')
            p++;
        int len = (int)(p - s);
        if (len > 127) len = 127;
        memcpy(line_buf[nl], s, (size_t)len);
        line_buf[nl][len] = '\0';
        lines[nl] = line_buf[nl];
        nl++;

        while (*p == '\n' || *p == '\r')
            p++;
    }

    return nl;
}

/* ── Region-to-seed conversion ───────────────────────────────── */

/**
 * @brief Map an ASCII region type character to an srd_room_type_t.
 */
static srd_room_type_t char_to_room_type(char c) {
    switch (c) {
    case 'E': return SRD_ROOM_ENTRANCE;
    case 'B': return SRD_ROOM_BOSS;
    case 'T': return SRD_ROOM_TREASURE;
    case 'P': return SRD_ROOM_PRIVATE;
    case 'S': return SRD_ROOM_SECRET;
    case 'C': return SRD_ROOM_CORRIDOR;
    case 'D': return SRD_ROOM_DEAD_END;
    default:  return SRD_ROOM_GENERIC;
    }
}

/**
 * @brief Convert parsed grid regions to seed rooms for SDF initialization.
 *
 * Each region becomes a 3D box: the XZ extent is derived from the region's
 * cell coverage in the ASCII grid (cells mapped to meters), and Y is a
 * fixed height. Adjacency pairs come from the grid's edge list.
 */
static int grid_to_seeds(const srd_grid_t *grid,
                         srd_seed_room_t *rooms, int max_rooms,
                         int *adj_pairs, int *n_pairs, int max_pairs) {
    int nr = grid->n_regions;
    if (nr < 1 || nr > max_rooms) return -1;

    float cell_size = 2.0f;  /* Meters per ASCII cell */
    float room_hy   = 2.0f;  /* Room half-height (4m total) */
    float room_cy   = 2.0f;  /* Room center Y (floor at 0, ceiling at 4) */

    for (int ri = 0; ri < nr; ri++) {
        /* Compute bounding box of region cells in grid space */
        int min_x = grid->width, max_x = -1;
        int min_z = grid->height, max_z = -1;

        for (int z = 0; z < grid->height; z++) {
            for (int x = 0; x < grid->width; x++) {
                if (grid->region_ids[z * grid->width + x] == ri) {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (z < min_z) min_z = z;
                    if (z > max_z) max_z = z;
                }
            }
        }

        if (max_x < 0) return -1; /* Region has no cells */

        /* Convert to world-space box */
        float wx0 = (float)min_x * cell_size;
        float wx1 = (float)(max_x + 1) * cell_size;
        float wz0 = (float)min_z * cell_size;
        float wz1 = (float)(max_z + 1) * cell_size;

        rooms[ri].cx = (wx0 + wx1) * 0.5f;
        rooms[ri].cy = room_cy;
        rooms[ri].cz = (wz0 + wz1) * 0.5f;
        rooms[ri].hx = (wx1 - wx0) * 0.5f;
        rooms[ri].hy = room_hy;
        rooms[ri].hz = (wz1 - wz0) * 0.5f;
        rooms[ri].type = char_to_room_type(grid->regions[ri].type_char);
    }

    /* Copy adjacency edges */
    int np = 0;
    for (int i = 0; i < grid->n_edges && np < max_pairs; i++) {
        adj_pairs[np * 2 + 0] = grid->edges[i].a;
        adj_pairs[np * 2 + 1] = grid->edges[i].b;
        np++;
    }
    *n_pairs = np;

    return nr;
}

/* ── Public API ──────────────────────────────────────────────── */

int srd_generate_svo(const char *ascii, uint32_t seed, double time_budget,
                     npc_svo_grid_t *svo_out) {
    if (!ascii || !svo_out) return -1;
    if (!*ascii) return -1;

    if (seed)
        srand(seed);
    else
        srand((uint32_t)time(NULL));

    /* Step 1: Parse ASCII into grid */
    char line_buf[64][128];
    const char *lines[64];
    int nl = parse_ascii_lines(ascii, line_buf, lines, 64);
    if (nl < 1) return -1;

    srd_grid_t grid;
    if (srd_grid_parse(lines, nl, &grid) != 0)
        return -1;

    if (grid.n_regions < 1) {
        free(grid.cells);
        free(grid.region_ids);
        free(grid.labels);
        return -1;
    }

    /* Step 2: Convert grid regions to seed rooms */
    srd_seed_room_t rooms[128];
    int adj_pairs[512];
    int n_pairs = 0;

    int n_rooms = grid_to_seeds(&grid, rooms, 128, adj_pairs, &n_pairs, 256);

    free(grid.cells);
    free(grid.region_ids);
    free(grid.labels);

    if (n_rooms < 1) return -1;

    /* Step 3: Initialize SDF grid + room map from seed rooms */
    srd_sdf_grid_t sdf_grid;
    srd_room_map_t room_map;

    float voxel_size = 0.5f;
    float margin = 2.0f;

    int rc = srd_seed_to_grid(rooms, n_rooms, adj_pairs, n_pairs,
                              voxel_size, margin, &sdf_grid, &room_map);
    if (rc != 0) return -1;

    /* Step 4: Run descent optimization */
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, time_budget > 0 ? time_budget : 3.0);

    int n_rules = 0;
    const srd_voxel_rule_entry_t *rule_table = srd_voxel_rule_table_default(&n_rules);
    cfg.rules = rule_table;
    cfg.n_rules = n_rules;
    cfg.verbose = 1;

    srd_descent_result_t result = srd_descent_optimize(&sdf_grid, &room_map, &cfg);

    fprintf(stderr, "[bridge] optimize: %d iters, loss %.4f → %.4f\n",
            result.iterations, result.initial_loss, result.final_loss);

    /* Step 5: Convert SDF grid to SVO */
    rc = srd_sdf_to_svo(&sdf_grid, svo_out);

    srd_sdf_grid_destroy(&sdf_grid);
    srd_room_map_destroy(&room_map);

    return rc;
}

int srd_generate(const char *ascii, uint32_t seed, double time_budget,
                 fr_room_box_t **rooms_out, uint32_t *n_rooms_out,
                 fr_corridor_seg_t **corridors_out, uint32_t *n_corridors_out) {
    if (!rooms_out || !n_rooms_out || !corridors_out || !n_corridors_out)
        return -1;

    /* The new pipeline produces SVO output, not tiles.
     * This legacy wrapper runs the pipeline and discards the SVO. */
    *rooms_out = NULL;
    *n_rooms_out = 0;
    *corridors_out = NULL;
    *n_corridors_out = 0;

    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));
    int rc = srd_generate_svo(ascii, seed, time_budget, &svo);
    npc_svo_grid_destroy(&svo);

    return rc;
}

void srd_free_geometry(fr_room_box_t *rooms, fr_corridor_seg_t *corridors) {
    (void)corridors;
    free(rooms);
}
