/**
 * @file srd_rules_vcorridor.c
 * @brief Voxel corridor widen and narrow rewrite rules.
 *
 * Non-static functions (2):
 *   srd_rule_corridor_widen
 *   srd_rule_corridor_narrow
 */
#include "ferrum/procgen/srd/srd_rules_vcorridor.h"

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

/**
 * @brief Detect the narrow axis of a corridor.
 * @return 0 for X-narrow (long in Z), 1 for Z-narrow (long in X).
 */
static int detect_narrow_axis(int x0, int z0, int x1, int z1) {
    int wx = x1 - x0 + 1;
    int wz = z1 - z0 + 1;
    return (wz <= wx) ? 1 : 0;  /* 1 = Z-narrow, 0 = X-narrow */
}

/* ── Widen ────────────────────────────────────────────────────── */

int srd_rule_corridor_widen(srd_sdf_grid_t *grid, srd_room_map_t *map,
                            const srd_voxel_selection_t *sel) {
    if (validate_inputs(grid, map, sel) != 0) return -1;

    int param = (int)sel->param;
    if (param <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;
    int narrow_axis = detect_narrow_axis(x0, z0, x1, z1);

    /* Expand on both sides of the narrow axis */
    if (narrow_axis == 1) {
        /* Z is narrow — expand Z range */
        int lo = z0 - param;
        int hi = z1 + param;
        if (lo < 0) lo = 0;
        if (hi >= grid->nz) hi = grid->nz - 1;

        /* Low side: z in [lo, z0-1] */
        for (int z = lo; z < z0; z++)
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    int idx = z * ny * nx + y * nx + x;
                    if (map->ids[idx] == 0 && grid->values[idx] >= 0.0f) {
                        grid->values[idx] = -grid->voxel_size;
                        map->ids[idx] = sel->room_id;
                    }
                }
        /* High side: z in [z1+1, hi] */
        for (int z = z1 + 1; z <= hi; z++)
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    int idx = z * ny * nx + y * nx + x;
                    if (map->ids[idx] == 0 && grid->values[idx] >= 0.0f) {
                        grid->values[idx] = -grid->voxel_size;
                        map->ids[idx] = sel->room_id;
                    }
                }
    } else {
        /* X is narrow — expand X range */
        int lo = x0 - param;
        int hi = x1 + param;
        if (lo < 0) lo = 0;
        if (hi >= grid->nx) hi = grid->nx - 1;

        for (int z = z0; z <= z1; z++)
            for (int y = y0; y <= y1; y++) {
                for (int x = lo; x < x0; x++) {
                    int idx = z * ny * nx + y * nx + x;
                    if (map->ids[idx] == 0 && grid->values[idx] >= 0.0f) {
                        grid->values[idx] = -grid->voxel_size;
                        map->ids[idx] = sel->room_id;
                    }
                }
                for (int x = x1 + 1; x <= hi; x++) {
                    int idx = z * ny * nx + y * nx + x;
                    if (map->ids[idx] == 0 && grid->values[idx] >= 0.0f) {
                        grid->values[idx] = -grid->voxel_size;
                        map->ids[idx] = sel->room_id;
                    }
                }
            }
    }

    return 0;
}

/* ── Narrow ───────────────────────────────────────────────────── */

int srd_rule_corridor_narrow(srd_sdf_grid_t *grid, srd_room_map_t *map,
                             const srd_voxel_selection_t *sel) {
    if (validate_inputs(grid, map, sel) != 0) return -1;

    int param = (int)sel->param;
    if (param <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;
    int narrow_axis = detect_narrow_axis(x0, z0, x1, z1);

    /* Shrink on both sides of the narrow axis */
    if (narrow_axis == 1) {
        /* Z is narrow — shrink Z range */
        /* Low side: z in [z0, z0+param-1] */
        int lo_end = z0 + param - 1;
        if (lo_end > z1) lo_end = z1;
        for (int z = z0; z <= lo_end; z++)
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    int idx = z * ny * nx + y * nx + x;
                    if (map->ids[idx] == sel->room_id) {
                        grid->values[idx] = grid->voxel_size;
                        map->ids[idx] = 0;
                    }
                }
        /* High side: z in [z1-param+1, z1] */
        int hi_start = z1 - param + 1;
        if (hi_start < z0) hi_start = z0;
        for (int z = hi_start; z <= z1; z++)
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    int idx = z * ny * nx + y * nx + x;
                    if (map->ids[idx] == sel->room_id) {
                        grid->values[idx] = grid->voxel_size;
                        map->ids[idx] = 0;
                    }
                }
    } else {
        /* X is narrow — shrink X range */
        int lo_end = x0 + param - 1;
        if (lo_end > x1) lo_end = x1;
        int hi_start = x1 - param + 1;
        if (hi_start < x0) hi_start = x0;

        for (int z = z0; z <= z1; z++)
            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= lo_end; x++) {
                    int idx = z * ny * nx + y * nx + x;
                    if (map->ids[idx] == sel->room_id) {
                        grid->values[idx] = grid->voxel_size;
                        map->ids[idx] = 0;
                    }
                }
                for (int x = hi_start; x <= x1; x++) {
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
