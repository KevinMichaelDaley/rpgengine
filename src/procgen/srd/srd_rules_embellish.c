/**
 * @file srd_rules_embellish.c
 * @brief Embellishment rules: alcove, floor pit, floor pit fill.
 *
 * Non-static functions (3):
 *   srd_rule_alcove
 *   srd_rule_floor_pit
 *   srd_rule_floor_pit_fill
 */
#include "ferrum/procgen/srd/srd_rules_embellish.h"

#include <math.h>

/* ── Helpers ──────────────────────────────────────────────────── */

static int validate_wall_inputs(const srd_sdf_grid_t *grid,
                                const srd_room_map_t *map,
                                const srd_voxel_selection_t *sel) {
    if (!grid || !grid->values || !map || !map->ids || !sel) return -1;
    if (sel->room_id < 1 || sel->room_id > map->n_rooms) return -1;
    if (sel->face < SRD_FACE_NORTH || sel->face > SRD_FACE_WEST) return -1;
    return 0;
}

static int validate_floor_inputs(const srd_sdf_grid_t *grid,
                                 const srd_room_map_t *map,
                                 const srd_voxel_selection_t *sel) {
    if (!grid || !grid->values || !map || !map->ids || !sel) return -1;
    if (sel->room_id < 1 || sel->room_id > map->n_rooms) return -1;
    if (sel->face != SRD_FACE_FLOOR) return -1;
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

/* ── Alcove ───────────────────────────────────────────────────── */

int srd_rule_alcove(srd_sdf_grid_t *grid, srd_room_map_t *map,
                    const srd_voxel_selection_t *sel) {
    if (validate_wall_inputs(grid, map, sel) != 0) return -1;

    int depth = (int)sel->param;
    if (depth <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Alcove: semicircular recess into the wall, spanning the middle
     * half of the wall's height. The semicircle is in the depth-vs-lateral
     * plane, centered on the wall face. */

    int room_h = y1 - y0 + 1;
    int alcove_h_half = room_h / 4;
    if (alcove_h_half < 1) alcove_h_half = 1;
    int y_mid = (y0 + y1) / 2;
    int ay0 = y_mid - alcove_h_half;
    int ay1 = y_mid + alcove_h_half;

    /* Determine lateral range and depth direction */
    int lat_mid, lat_half;
    float r2 = (float)(depth * depth);

    switch (sel->face) {
    case SRD_FACE_EAST: {
        int room_wz = z1 - z0 + 1;
        lat_mid = (z0 + z1) / 2;
        lat_half = room_wz / 4;
        if (lat_half < 1) lat_half = 1;

        for (int d = 0; d < depth; d++) {
            int x = x1 + 1 + d;
            if (x >= grid->nx) break;
            /* Semicircle radius at this depth */
            float dx = (float)d + 0.5f;
            float max_lat2 = r2 - dx * dx;
            if (max_lat2 < 0) continue;
            float max_lat = sqrtf(max_lat2);
            int lat_range = (int)max_lat;
            if (lat_range > lat_half) lat_range = lat_half;

            for (int z = lat_mid - lat_range; z <= lat_mid + lat_range; z++) {
                if (z < 0 || z >= grid->nz) continue;
                for (int y = ay0; y <= ay1; y++) {
                    if (y < 0 || y >= grid->ny) continue;
                    int idx = z * ny * nx + y * nx + x;
                    if (grid->values[idx] >= 0.0f) {
                        grid->values[idx] = -grid->voxel_size;
                        map->ids[idx] = sel->room_id;
                    }
                }
            }
        }
        break;
    }
    case SRD_FACE_WEST: {
        int room_wz = z1 - z0 + 1;
        lat_mid = (z0 + z1) / 2;
        lat_half = room_wz / 4;
        if (lat_half < 1) lat_half = 1;

        for (int d = 0; d < depth; d++) {
            int x = x0 - 1 - d;
            if (x < 0) break;
            float dx = (float)d + 0.5f;
            float max_lat2 = r2 - dx * dx;
            if (max_lat2 < 0) continue;
            float max_lat = sqrtf(max_lat2);
            int lat_range = (int)max_lat;
            if (lat_range > lat_half) lat_range = lat_half;

            for (int z = lat_mid - lat_range; z <= lat_mid + lat_range; z++) {
                if (z < 0 || z >= grid->nz) continue;
                for (int y = ay0; y <= ay1; y++) {
                    if (y < 0 || y >= grid->ny) continue;
                    int idx = z * ny * nx + y * nx + x;
                    if (grid->values[idx] >= 0.0f) {
                        grid->values[idx] = -grid->voxel_size;
                        map->ids[idx] = sel->room_id;
                    }
                }
            }
        }
        break;
    }
    case SRD_FACE_SOUTH: {
        int room_wx = x1 - x0 + 1;
        lat_mid = (x0 + x1) / 2;
        lat_half = room_wx / 4;
        if (lat_half < 1) lat_half = 1;

        for (int d = 0; d < depth; d++) {
            int z = z1 + 1 + d;
            if (z >= grid->nz) break;
            float dz = (float)d + 0.5f;
            float max_lat2 = r2 - dz * dz;
            if (max_lat2 < 0) continue;
            float max_lat = sqrtf(max_lat2);
            int lat_range = (int)max_lat;
            if (lat_range > lat_half) lat_range = lat_half;

            for (int x = lat_mid - lat_range; x <= lat_mid + lat_range; x++) {
                if (x < 0 || x >= grid->nx) continue;
                for (int y = ay0; y <= ay1; y++) {
                    if (y < 0 || y >= grid->ny) continue;
                    int idx = z * ny * nx + y * nx + x;
                    if (grid->values[idx] >= 0.0f) {
                        grid->values[idx] = -grid->voxel_size;
                        map->ids[idx] = sel->room_id;
                    }
                }
            }
        }
        break;
    }
    case SRD_FACE_NORTH: {
        int room_wx = x1 - x0 + 1;
        lat_mid = (x0 + x1) / 2;
        lat_half = room_wx / 4;
        if (lat_half < 1) lat_half = 1;

        for (int d = 0; d < depth; d++) {
            int z = z0 - 1 - d;
            if (z < 0) break;
            float dz = (float)d + 0.5f;
            float max_lat2 = r2 - dz * dz;
            if (max_lat2 < 0) continue;
            float max_lat = sqrtf(max_lat2);
            int lat_range = (int)max_lat;
            if (lat_range > lat_half) lat_range = lat_half;

            for (int x = lat_mid - lat_range; x <= lat_mid + lat_range; x++) {
                if (x < 0 || x >= grid->nx) continue;
                for (int y = ay0; y <= ay1; y++) {
                    if (y < 0 || y >= grid->ny) continue;
                    int idx = z * ny * nx + y * nx + x;
                    if (grid->values[idx] >= 0.0f) {
                        grid->values[idx] = -grid->voxel_size;
                        map->ids[idx] = sel->room_id;
                    }
                }
            }
        }
        break;
    }
    default:
        return -1;
    }

    return 0;
}

/* ── Floor Pit ────────────────────────────────────────────────── */

int srd_rule_floor_pit(srd_sdf_grid_t *grid, srd_room_map_t *map,
                       const srd_voxel_selection_t *sel) {
    if (validate_floor_inputs(grid, map, sel) != 0) return -1;

    int depth = (int)sel->param;
    if (depth <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Pit in center half of XZ footprint, extending below floor */
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

    /* Carve below floor: y from y0-depth to y0-1 */
    int sy0 = y0 - depth;
    int sy1 = y0 - 1;
    if (sy0 < 0) sy0 = 0;

    for (int z = sz0; z <= sz1; z++) {
        for (int y = sy0; y <= sy1; y++) {
            for (int x = sx0; x <= sx1; x++) {
                int idx = z * ny * nx + y * nx + x;
                if (grid->values[idx] >= 0.0f && map->ids[idx] == 0) {
                    grid->values[idx] = -grid->voxel_size;
                    map->ids[idx] = sel->room_id;
                }
            }
        }
    }

    return 0;
}

/* ── Floor Pit Fill ───────────────────────────────────────────── */

int srd_rule_floor_pit_fill(srd_sdf_grid_t *grid, srd_room_map_t *map,
                            const srd_voxel_selection_t *sel) {
    if (validate_floor_inputs(grid, map, sel) != 0) return -1;

    int depth = (int)sel->param;
    if (depth <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Fill the pit region: the bottom `depth` voxels of the room's Y extent,
     * limited to the center half of the XZ footprint. After pit was carved,
     * y0 moved down by `depth`. The pit region is y=[y0, y0+depth-1]. */
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

    int sy0 = y0;
    int sy1 = y0 + depth - 1;
    if (sy1 > y1) sy1 = y1;

    for (int z = sz0; z <= sz1; z++) {
        for (int y = sy0; y <= sy1; y++) {
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
