/**
 * @file srd_rules_corner.c
 * @brief Corner chamfer and round rewrite rules.
 *
 * Non-static functions (2):
 *   srd_rule_corner_chamfer
 *   srd_rule_corner_round
 */
#include "ferrum/procgen/srd/srd_rules_corner.h"

#include <math.h>

/* ── Helpers ──────────────────────────────────────────────────── */

static int validate_corner_inputs(const srd_sdf_grid_t *grid,
                                  const srd_room_map_t *map,
                                  const srd_voxel_selection_t *sel) {
    if (!grid || !grid->values || !map || !map->ids || !sel) return -1;
    if (sel->room_id < 1 || sel->room_id > map->n_rooms) return -1;
    if (sel->corner < 0 || sel->corner > 3) return -1;
    return 0;
}

static int find_room_bbox(const srd_room_map_t *map, uint8_t room_id,
                          int *x0, int *y0, int *z0,
                          int *x1, int *y1, int *z1) {
    int nx = map->nx, ny = map->ny, nz = map->nz;
    int found = 0;
    int rx0 = nx, ry0 = ny, rz0 = nz;
    int rx1 = -1, ry1 = -1, rz1 = -1;
    for (int z = 0; z < nz; z++)
        for (int y = 0; y < ny; y++)
            for (int x = 0; x < nx; x++)
                if (map->ids[z * ny * nx + y * nx + x] == room_id) {
                    found = 1;
                    if (x < rx0) rx0 = x;
                    if (y < ry0) ry0 = y;
                    if (z < rz0) rz0 = z;
                    if (x > rx1) rx1 = x;
                    if (y > ry1) ry1 = y;
                    if (z > rz1) rz1 = z;
                }
    if (!found) return -1;
    *x0 = rx0; *y0 = ry0; *z0 = rz0;
    *x1 = rx1; *y1 = ry1; *z1 = rz1;
    return 0;
}

/* ── Chamfer ──────────────────────────────────────────────────── */

int srd_rule_corner_chamfer(srd_sdf_grid_t *grid, srd_room_map_t *map,
                            const srd_voxel_selection_t *sel) {
    if (validate_corner_inputs(grid, map, sel) != 0) return -1;

    int cut = (int)sel->param;
    if (cut <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Corner indices: 0=NE (+X,-Z), 1=NW (-X,-Z), 2=SE (+X,+Z), 3=SW (-X,+Z).
     * For chamfer, fill voxels where dx + dz < cut (measured from corner). */

    for (int y = y0; y <= y1; y++) {
        for (int i = 0; i < cut; i++) {
            for (int j = 0; j < cut - i; j++) {
                int x, z;
                switch (sel->corner) {
                case 0: /* NE: corner at (x1, z0) */
                    x = x1 - i; z = z0 + j; break;
                case 1: /* NW: corner at (x0, z0) */
                    x = x0 + i; z = z0 + j; break;
                case 2: /* SE: corner at (x1, z1) */
                    x = x1 - i; z = z1 - j; break;
                case 3: /* SW: corner at (x0, z1) */
                    x = x0 + i; z = z1 - j; break;
                default: continue;
                }

                if (x < 0 || x >= grid->nx || z < 0 || z >= grid->nz) continue;

                int idx = z * ny * nx + y * nx + x;
                if (map->ids[idx] == sel->room_id) {
                    grid->values[idx] = grid->voxel_size;
                    map->ids[idx] = 0;
                }
            }
        }
    }

    return 0;
}

/* ── Round ────────────────────────────────────────────────────── */

int srd_rule_corner_round(srd_sdf_grid_t *grid, srd_room_map_t *map,
                          const srd_voxel_selection_t *sel) {
    if (validate_corner_inputs(grid, map, sel) != 0) return -1;

    int radius = (int)sel->param;
    if (radius <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;
    float r2 = (float)(radius * radius);

    /* For round, fill voxels where dx² + dz² > r² (outside the arc).
     * The arc center is placed `radius` voxels from the corner
     * along each wall. */

    for (int y = y0; y <= y1; y++) {
        for (int i = 0; i < radius; i++) {
            for (int j = 0; j < radius; j++) {
                /* Distance from the arc center */
                float di = (float)(radius - i) - 0.5f;
                float dj = (float)(radius - j) - 0.5f;
                if (di * di + dj * dj <= r2) continue; /* inside arc — keep */

                int x, z;
                switch (sel->corner) {
                case 0: x = x1 - i; z = z0 + j; break;
                case 1: x = x0 + i; z = z0 + j; break;
                case 2: x = x1 - i; z = z1 - j; break;
                case 3: x = x0 + i; z = z1 - j; break;
                default: continue;
                }

                if (x < 0 || x >= grid->nx || z < 0 || z >= grid->nz) continue;

                int idx = z * ny * nx + y * nx + x;
                if (map->ids[idx] == sel->room_id) {
                    grid->values[idx] = grid->voxel_size;
                    map->ids[idx] = 0;
                }
            }
        }
    }

    return 0;
}
