/**
 * @file srd_bridge.c
 * @brief SRD bridge: ASCII floor plan → SDF grid → optimize → SVO.
 *
 * Non-static functions (4): srd_generate_svo, srd_generate_svo_ex,
 *                            srd_generate, srd_free_geometry
 */
#include "ferrum/procgen/srd/srd_bridge.h"
#include "ferrum/procgen/srd/srd_grammar.h"
#include "ferrum/procgen/srd/srd_seed_init.h"
#include "ferrum/procgen/srd/srd_descent_loop.h"
#include "ferrum/procgen/srd/srd_descent_config.h"
#include "ferrum/procgen/srd/srd_voxel_rule_table.h"
#include "ferrum/procgen/srd/srd_sdf_to_svo.h"
#include "ferrum/procgen/srd/srd_room_type.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── ASCII parsing ──────────────────────────────────────────────── */

/**
 * @brief Parse ASCII string into lines, tracking floor boundaries.
 *
 * Splits the ASCII string on newlines, recording where each floor
 * starts (triggered by === FLOOR N: NAME === headers). Returns the
 * number of grid lines parsed (up to max_lines).
 *
 * @param ascii        Input ASCII string.
 * @param line_buf     Output line storage.
 * @param lines        Output line pointer array.
 * @param max_lines    Max lines to parse.
 * @param floor_starts Output: grid row index where each floor begins.
 * @param n_floors     Output: number of floors detected.
 * @return Number of grid lines parsed.
 */
static int parse_ascii_lines(const char *ascii,
                             char line_buf[][128], const char **lines,
                             int max_lines,
                             int *floor_starts, int *n_floors) {
    const char *p = ascii;
    int nl = 0;
    int nf = 0;

    while (*p && nl < max_lines) {
        /* Skip blank lines */
        while (*p == '\n' || *p == '\r')
            p++;
        if (!*p) break;

        /* === header: record that a new floor starts at this line index */
        if (*p == '=' && p[1] == '=' && p[2] == '=') {
            if (nf < 16)
                floor_starts[nf++] = nl;
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

    /* If no headers found, treat entire grid as floor 0 */
    if (nf == 0) {
        floor_starts[0] = 0;
        nf = 1;
    }
    *n_floors = nf;

    return nl;
}

/* ── Config defaults ────────────────────────────────────────────── */

/**
 * @brief Fill zero-valued config fields with sensible defaults.
 */
static srd_dungeon_config_t config_with_defaults(const srd_dungeon_config_t *cfg) {
    srd_dungeon_config_t out;
    if (cfg)
        out = *cfg;
    else
        memset(&out, 0, sizeof(out));

    if (out.cell_size     <= 0.0f) out.cell_size     = 2.0f;
    if (out.room_height   <= 0.0f) out.room_height   = 4.0f;
    if (out.floor_spacing <= 0.0f) out.floor_spacing  = 5.0f;
    if (out.voxel_size    <= 0.0f) out.voxel_size    = 0.5f;
    if (out.margin        <= 0.0f) out.margin        = 2.0f;
    if (out.stair_steps   <= 0)    out.stair_steps   = 8;

    return out;
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
 * @brief Determine which floor a grid row belongs to.
 *
 * Returns the floor index (0-based) for a given grid row, based on
 * the recorded floor boundary positions.
 */
static int row_to_floor(int row, const int *floor_starts, int n_floors) {
    int floor_idx = 0;
    for (int f = n_floors - 1; f >= 0; f--) {
        if (row >= floor_starts[f]) {
            floor_idx = f;
            break;
        }
    }
    return floor_idx;
}

/**
 * @brief Convert parsed grid regions to seed rooms for SDF initialization.
 *
 * Each region becomes a 3D box. The XZ extent is derived from the region's
 * cell coverage (relative to its floor's start row). The Y position is
 * offset by floor index * floor_spacing so multi-floor dungeons stack
 * vertically with a solid slab gap between them.
 * Cross-floor adjacency edges (stair connections) are excluded — those
 * are handled separately by carve_stairs().
 */
static int grid_to_seeds(const srd_grid_t *grid,
                         const int *floor_starts, int n_floors,
                         const srd_dungeon_config_t *dcfg,
                         srd_seed_room_t *rooms, int max_rooms,
                         int *adj_pairs, int *n_pairs, int max_pairs) {
    int nr = grid->n_regions;
    if (nr < 1 || nr > max_rooms) return -1;

    float cell_size     = dcfg->cell_size;
    float room_hy       = dcfg->room_height * 0.5f;   /* Half-height of room interior */
    float floor_spacing = dcfg->floor_spacing;         /* Vertical center-to-center */
    float room_cy_base  = room_hy;                     /* Center Y for floor 0 */

    for (int ri = 0; ri < nr; ri++) {
        /* Determine which floor this region belongs to */
        int floor_idx = row_to_floor(grid->regions[ri].first_z,
                                     floor_starts, n_floors);
        int floor_z_start = floor_starts[floor_idx];

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

        /* Convert to world-space box (Z relative to floor start) */
        float wx0 = (float)min_x * cell_size;
        float wx1 = (float)(max_x + 1) * cell_size;
        float wz0 = (float)(min_z - floor_z_start) * cell_size;
        float wz1 = (float)(max_z - floor_z_start + 1) * cell_size;

        rooms[ri].cx = (wx0 + wx1) * 0.5f;
        rooms[ri].cy = room_cy_base + (float)floor_idx * floor_spacing;
        rooms[ri].cz = (wz0 + wz1) * 0.5f;
        rooms[ri].hx = (wx1 - wx0) * 0.5f;
        rooms[ri].hy = room_hy;
        rooms[ri].hz = (wz1 - wz0) * 0.5f;
        rooms[ri].type = char_to_room_type(grid->regions[ri].type_char);

        fprintf(stderr, "[bridge] room %d '%c': floor=%d cy=%.1f cz=%.1f "
                "hx=%.1f hz=%.1f (first_z=%d floor_z_start=%d)\n",
                ri, grid->regions[ri].type_char, floor_idx,
                rooms[ri].cy, rooms[ri].cz,
                rooms[ri].hx, rooms[ri].hz,
                grid->regions[ri].first_z, floor_z_start);
    }

    /* Copy adjacency edges, excluding cross-floor stair edges */
    int np = 0;
    for (int i = 0; i < grid->n_edges && np < max_pairs; i++) {
        int a = grid->edges[i].a;
        int b = grid->edges[i].b;
        if (a < 0 || a >= nr || b < 0 || b >= nr) continue;

        /* Skip edges between rooms on different floors (stair connections) */
        if (fabsf(rooms[a].cy - rooms[b].cy) > 0.1f) continue;

        adj_pairs[np * 2 + 0] = a;
        adj_pairs[np * 2 + 1] = b;
        np++;
    }
    *n_pairs = np;

    return nr;
}

/* ── Stair carving ──────────────────────────────────────────────── */

/**
 * @brief Carve stair shafts and stamp step geometry between floors.
 *
 * Scans the parsed ASCII grid for ^ (stair up) and v (stair down) cells
 * at matching x columns. For each pair, carves a tall vertical shaft and
 * a long horizontal run, then stamps individual stair treads as solid
 * blocks inside the carved air volume.
 *
 * The stair run extends along Z, using enough length so each tread is
 * at least 2 voxels deep (resolvable in the SDF grid). The shaft is
 * one cell wide in X.
 */
static void carve_stairs(const srd_grid_t *grid,
                         const int *floor_starts, int n_floors,
                         const srd_dungeon_config_t *dcfg,
                         srd_sdf_grid_t *sdf_grid) {
    if (n_floors < 2) return;

    float cell_size     = dcfg->cell_size;
    float room_height   = dcfg->room_height;
    float floor_spacing = dcfg->floor_spacing;
    float voxel_size    = dcfg->voxel_size;
    int   n_steps       = dcfg->stair_steps;

    float min_tread_depth = voxel_size * 2.0f;

    for (int z1 = 0; z1 < grid->height; z1++) {
        for (int x1 = 0; x1 < grid->width; x1++) {
            if (grid->cells[z1 * grid->width + x1] != '^') continue;

            int floor_a = row_to_floor(z1, floor_starts, n_floors);

            for (int z2 = 0; z2 < grid->height; z2++) {
                if (grid->cells[z2 * grid->width + x1] != 'v') continue;

                int floor_b = row_to_floor(z2, floor_starts, n_floors);
                if (floor_b != floor_a + 1) continue;

                float wx = (float)x1 * cell_size + cell_size * 0.5f;

                /* Z positions: ^ on floor_a, v on floor_b (both floor-relative) */
                float wz_a = (float)(z1 - floor_starts[floor_a]) * cell_size
                             + cell_size * 0.5f;
                float wz_b = (float)(z2 - floor_starts[floor_b]) * cell_size
                             + cell_size * 0.5f;

                /* Y: bottom step at floor_a ground, top step at floor_b ground */
                float y_lo = (float)floor_a * floor_spacing;
                float y_hi = (float)floor_b * floor_spacing + room_height;
                float total_rise = (float)floor_b * floor_spacing - y_lo;

                /* Stair run: the top step must land at wz_b (inside the
                 * v cell's room). The run extends backward from wz_b
                 * so the bottom step is near wz_a. If the natural Z
                 * distance is too short, extend the run to ensure each
                 * tread is at least min_tread_depth. */
                float needed_run = min_tread_depth * (float)n_steps;
                float tread_depth = needed_run / (float)n_steps;

                /* Top step at wz_b, bottom step at wz_b - run_length.
                 * Add half a cell of padding at each end for the shaft. */
                float run_z_end   = wz_b + cell_size * 0.5f;
                float run_z_start = wz_b - needed_run - cell_size * 0.5f;
                float run_cz      = (run_z_start + run_z_end) * 0.5f;
                float run_hz      = (run_z_end - run_z_start) * 0.5f;

                /* Shaft: one cell wide, full run deep, full height */
                float shaft_hx = cell_size * 0.5f;
                float shaft_cy = (y_lo + y_hi) * 0.5f;
                float shaft_hy = (y_hi - y_lo) * 0.5f;

                srd_sdf_grid_stamp_box(sdf_grid,
                                       wx, shaft_cy, run_cz,
                                       shaft_hx, shaft_hy, run_hz);

                /* Stamp treads. Step 0 is at the bottom (low Z), step
                 * n_steps-1 is at wz_b (inside floor_b's room). */
                float step_rise   = total_rise / (float)n_steps;
                float tread_thick = step_rise * 0.5f;
                float min_thick   = voxel_size * 2.0f;
                if (tread_thick < min_thick) tread_thick = min_thick;

                /* Bottom of the tread run: top step center is at wz_b */
                float tread_z_base = wz_b - (float)(n_steps - 1) * tread_depth;

                for (int s = 0; s < n_steps; s++) {
                    float step_y   = y_lo + (float)(s + 1) * step_rise;
                    float tread_cy = step_y - tread_thick * 0.5f;
                    float tread_hy = tread_thick * 0.5f;
                    float tread_z  = tread_z_base + (float)s * tread_depth;

                    srd_sdf_grid_subtract_box(sdf_grid,
                                              wx, tread_cy, tread_z,
                                              shaft_hx, tread_hy,
                                              tread_depth * 0.5f);
                }

                fprintf(stderr,
                        "[bridge] stair: floor %d→%d at x=%.1f "
                        "z_run=%.1f→%.1f (^z=%.1f vz=%.1f) "
                        "y=%.1f→%.1f tread=%.2f×%.2f rise=%.2f\n",
                        floor_a, floor_b, wx,
                        run_z_start, run_z_end, wz_a, wz_b,
                        y_lo, y_hi,
                        tread_depth, tread_thick, step_rise);
            }
        }
    }
}

/* ── Cleanup helper ─────────────────────────────────────────────── */

static void free_grid(srd_grid_t *grid) {
    free(grid->cells);
    free(grid->region_ids);
    free(grid->labels);
}

/* ── Public API ──────────────────────────────────────────────── */

int srd_generate_svo(const char *ascii, uint32_t seed, double time_budget,
                     npc_svo_grid_t *svo_out) {
    return srd_generate_svo_ex(ascii, seed, time_budget, NULL, svo_out);
}

int srd_generate_svo_ex(const char *ascii, uint32_t seed, double time_budget,
                        const srd_dungeon_config_t *cfg,
                        npc_svo_grid_t *svo_out) {
    if (!ascii || !svo_out) return -1;
    if (!*ascii) return -1;

    srd_dungeon_config_t dcfg = config_with_defaults(cfg);

    if (seed)
        srand(seed);
    else
        srand((uint32_t)time(NULL));

    /* Step 1: Parse ASCII into grid, tracking floor boundaries */
    char line_buf[128][128];
    const char *lines[128];
    int floor_starts[16];
    int n_floors = 0;
    int nl = parse_ascii_lines(ascii, line_buf, lines, 128,
                               floor_starts, &n_floors);
    if (nl < 1) return -1;

    fprintf(stderr, "[bridge] parsed %d lines, %d floor(s)\n", nl, n_floors);
    fprintf(stderr, "[bridge] config: cell=%.1f room_h=%.1f floor_sp=%.1f "
            "voxel=%.2f margin=%.1f steps=%d\n",
            dcfg.cell_size, dcfg.room_height, dcfg.floor_spacing,
            dcfg.voxel_size, dcfg.margin, dcfg.stair_steps);

    srd_grid_t grid;
    if (srd_grid_parse(lines, nl, &grid) != 0)
        return -1;

    if (grid.n_regions < 1) {
        free_grid(&grid);
        return -1;
    }

    fprintf(stderr, "[bridge] grid: %dx%d, %d regions, %d edges\n",
            grid.width, grid.height, grid.n_regions, grid.n_edges);

    /* Step 2: Convert grid regions to seed rooms (with floor Y-offsets) */
    srd_seed_room_t rooms[128];
    int adj_pairs[512];
    int n_pairs = 0;

    int n_rooms = grid_to_seeds(&grid, floor_starts, n_floors, &dcfg,
                                rooms, 128, adj_pairs, &n_pairs, 256);
    if (n_rooms < 1) {
        free_grid(&grid);
        return -1;
    }

    /* Step 3: Initialize SDF grid + room map from seed rooms */
    srd_sdf_grid_t sdf_grid;
    srd_room_map_t room_map;

    int rc = srd_seed_to_grid(rooms, n_rooms, adj_pairs, n_pairs,
                              dcfg.voxel_size, dcfg.margin,
                              &sdf_grid, &room_map);
    if (rc != 0) {
        free_grid(&grid);
        return -1;
    }

    /* Step 4: Run descent optimization (before stairs, so optimizer
     * doesn't corrupt stair geometry) */
    srd_descent_config_t opt_cfg;
    srd_descent_config_from_budget(&opt_cfg, time_budget > 0 ? time_budget : 3.0);

    int n_rules = 0;
    const srd_voxel_rule_entry_t *rule_table = srd_voxel_rule_table_default(&n_rules);
    opt_cfg.rules = rule_table;
    opt_cfg.n_rules = n_rules;
    opt_cfg.verbose = 1;

    srd_descent_result_t result = srd_descent_optimize(&sdf_grid, &room_map, &opt_cfg);

    fprintf(stderr, "[bridge] optimize: %d iters, loss %.4f → %.4f\n",
            result.iterations, result.initial_loss, result.final_loss);

    /* Step 5: Carve stairs after optimization so treads stay clean */
#ifdef SRD_STAIR_GEN
    carve_stairs(&grid, floor_starts, n_floors, &dcfg, &sdf_grid);
#endif
    free_grid(&grid);

    /* Step 6: Convert SDF grid to SVO */
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
