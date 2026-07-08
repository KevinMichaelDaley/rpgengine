/**
 * @file srd_rules_height.c
 * @brief Ceiling raise/lower and floor step rewrite rules.
 *
 * Non-static functions (3):
 *   srd_rule_ceiling_raise
 *   srd_rule_ceiling_lower
 *   srd_rule_floor_step
 */
#include "ferrum/procgen/srd/srd_rules_height.h"

/* ── Helpers ──────────────────────────────────────────────────── */

static int validate_inputs(const srd_sdf_grid_t *grid,
                           const srd_room_map_t *map,
                           const srd_voxel_selection_t *sel) {
    if (!grid || !grid->values || !map || !map->ids || !sel) return -1;
    if (sel->room_id < 1 || sel->room_id > map->n_rooms) return -1;
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

/* ── Ceiling Raise ────────────────────────────────────────────── */

int srd_rule_ceiling_raise(srd_sdf_grid_t *grid, srd_room_map_t *map,
                           const srd_voxel_selection_t *sel) {
    if (validate_inputs(grid, map, sel) != 0) return -1;
    if (sel->face != SRD_FACE_CEIL) return -1;

    int param = (int)sel->param;
    if (param <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Carve voxels above current ceiling: Y from y1+1 to y1+param */
    int sy0 = y1 + 1;
    int sy1 = y1 + param;
    if (sy1 >= grid->ny) sy1 = grid->ny - 1;

    for (int z = z0; z <= z1; z++) {
        for (int y = sy0; y <= sy1; y++) {
            for (int x = x0; x <= x1; x++) {
                int idx = z * ny * nx + y * nx + x;
                if (grid->values[idx] >= 0.0f) {
                    grid->values[idx] = -grid->voxel_size;
                    map->ids[idx] = sel->room_id;
                }
            }
        }
    }

    return 0;
}

/* ── Ceiling Lower ────────────────────────────────────────────── */

int srd_rule_ceiling_lower(srd_sdf_grid_t *grid, srd_room_map_t *map,
                           const srd_voxel_selection_t *sel) {
    if (validate_inputs(grid, map, sel) != 0) return -1;
    if (sel->face != SRD_FACE_CEIL) return -1;

    int param = (int)sel->param;
    if (param <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Fill voxels at top of room: Y from y1-param+1 to y1 */
    int sy0 = y1 - param + 1;
    int sy1_val = y1;
    if (sy0 < y0) sy0 = y0;

    for (int z = z0; z <= z1; z++) {
        for (int y = sy0; y <= sy1_val; y++) {
            for (int x = x0; x <= x1; x++) {
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

/* ── Floor Step ───────────────────────────────────────────────── */

int srd_rule_floor_step(srd_sdf_grid_t *grid, srd_room_map_t *map,
                        const srd_voxel_selection_t *sel) {
    if (validate_inputs(grid, map, sel) != 0) return -1;
    if (sel->face != SRD_FACE_FLOOR) return -1;

    int height = (int)sel->param;
    if (height <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Step covers the center half of the XZ footprint */
    int room_wx = x1 - x0 + 1;
    int room_wz = z1 - z0 + 1;
    int x_margin = room_wx / 4;
    int z_margin = room_wz / 4;
    if (x_margin < 1) x_margin = 1;
    if (z_margin < 1) z_margin = 1;

    int sx0 = x0 + x_margin;
    int sx1 = x1 - x_margin;
    int sz0 = z0 + z_margin;
    int sz1 = z1 - z_margin;

    /* Fill from floor upward by height voxels */
    int sy0 = y0;
    int sy1_val = y0 + height - 1;
    if (sy1_val > y1) sy1_val = y1;

    for (int z = sz0; z <= sz1; z++) {
        for (int y = sy0; y <= sy1_val; y++) {
            for (int x = sx0; x <= sx1; x++) {
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
