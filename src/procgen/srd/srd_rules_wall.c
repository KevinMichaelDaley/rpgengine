/**
 * @file srd_rules_wall.c
 * @brief Wall push and pull rewrite rules.
 *
 * Non-static functions (2):
 *   srd_rule_wall_push
 *   srd_rule_wall_pull
 */
#include "ferrum/procgen/srd/srd_rules_wall.h"

/* ── Helpers ──────────────────────────────────────────────────── */

/**
 * @brief Validate common wall rule inputs.
 * @return 0 if valid, -1 if invalid.
 */
static int validate_wall_inputs(const srd_sdf_grid_t *grid,
                                const srd_room_map_t *map,
                                const srd_voxel_selection_t *sel) {
    if (!grid || !grid->values || !map || !map->ids || !sel) return -1;
    if (sel->room_id < 1 || sel->room_id > map->n_rooms) return -1;
    /* Wall rules only operate on N/S/E/W faces */
    if (sel->face < SRD_FACE_NORTH || sel->face > SRD_FACE_WEST) return -1;
    return 0;
}

/**
 * @brief Find the axis-aligned bounding box of a room in voxel coords.
 *
 * Scans all voxels to find min/max for the given room_id.
 * Returns 0 on success (room found), -1 if room has no voxels.
 */
static int find_room_bbox(const srd_room_map_t *map, uint8_t room_id,
                          int *x0, int *y0, int *z0,
                          int *x1, int *y1, int *z1) {
    int nx = map->nx, ny = map->ny, nz = map->nz;
    int found = 0;
    int rx0 = nx, ry0 = ny, rz0 = nz;
    int rx1 = -1, ry1 = -1, rz1 = -1;

    for (int z = 0; z < nz; z++) {
        for (int y = 0; y < ny; y++) {
            for (int x = 0; x < nx; x++) {
                if (map->ids[z * ny * nx + y * nx + x] == room_id) {
                    found = 1;
                    if (x < rx0) rx0 = x;
                    if (y < ry0) ry0 = y;
                    if (z < rz0) rz0 = z;
                    if (x > rx1) rx1 = x;
                    if (y > ry1) ry1 = y;
                    if (z > rz1) rz1 = z;
                }
            }
        }
    }

    if (!found) return -1;
    *x0 = rx0; *y0 = ry0; *z0 = rz0;
    *x1 = rx1; *y1 = ry1; *z1 = rz1;
    return 0;
}

/* ── Push ─────────────────────────────────────────────────────── */

int srd_rule_wall_push(srd_sdf_grid_t *grid, srd_room_map_t *map,
                       const srd_voxel_selection_t *sel) {
    if (validate_wall_inputs(grid, map, sel) != 0) return -1;

    int param = (int)sel->param;
    if (param <= 0) return 0; /* no-op */

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Determine the slice to fill in based on face direction.
     * Push = shrink room by filling voxels on the face side. */
    int sx0, sy0, sz0, sx1, sy1, sz1;
    switch (sel->face) {
    case SRD_FACE_EAST:  /* +X: fill from (x1-param+1..x1) */
        sx0 = x1 - param + 1; sx1 = x1;
        sy0 = y0; sy1 = y1; sz0 = z0; sz1 = z1;
        break;
    case SRD_FACE_WEST:  /* -X: fill from (x0..x0+param-1) */
        sx0 = x0; sx1 = x0 + param - 1;
        sy0 = y0; sy1 = y1; sz0 = z0; sz1 = z1;
        break;
    case SRD_FACE_SOUTH: /* +Z: fill from (z1-param+1..z1) */
        sx0 = x0; sx1 = x1;
        sy0 = y0; sy1 = y1;
        sz0 = z1 - param + 1; sz1 = z1;
        break;
    case SRD_FACE_NORTH: /* -Z: fill from (z0..z0+param-1) */
        sx0 = x0; sx1 = x1;
        sy0 = y0; sy1 = y1;
        sz0 = z0; sz1 = z0 + param - 1;
        break;
    default:
        return -1;
    }

    /* Clamp to grid bounds */
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sz0 < 0) sz0 = 0;
    if (sx1 >= grid->nx) sx1 = grid->nx - 1;
    if (sy1 >= grid->ny) sy1 = grid->ny - 1;
    if (sz1 >= grid->nz) sz1 = grid->nz - 1;

    /* Fill the slice: make solid, clear room ownership */
    for (int z = sz0; z <= sz1; z++) {
        for (int y = sy0; y <= sy1; y++) {
            for (int x = sx0; x <= sx1; x++) {
                int idx = z * ny * nx + y * nx + x;
                if (map->ids[idx] == sel->room_id) {
                    grid->values[idx] = grid->voxel_size; /* positive = solid */
                    map->ids[idx] = 0;
                }
            }
        }
    }

    return 0;
}

/* ── Pull ─────────────────────────────────────────────────────── */

int srd_rule_wall_pull(srd_sdf_grid_t *grid, srd_room_map_t *map,
                       const srd_voxel_selection_t *sel) {
    if (validate_wall_inputs(grid, map, sel) != 0) return -1;

    int param = (int)sel->param;
    if (param <= 0) return 0; /* no-op */

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Determine the slice to carve based on face direction.
     * Pull = expand room by carving voxels beyond the face. */
    int sx0, sy0, sz0, sx1, sy1, sz1;
    switch (sel->face) {
    case SRD_FACE_EAST:  /* +X: carve from (x1+1..x1+param) */
        sx0 = x1 + 1; sx1 = x1 + param;
        sy0 = y0; sy1 = y1; sz0 = z0; sz1 = z1;
        break;
    case SRD_FACE_WEST:  /* -X: carve from (x0-param..x0-1) */
        sx0 = x0 - param; sx1 = x0 - 1;
        sy0 = y0; sy1 = y1; sz0 = z0; sz1 = z1;
        break;
    case SRD_FACE_SOUTH: /* +Z: carve from (z1+1..z1+param) */
        sx0 = x0; sx1 = x1;
        sy0 = y0; sy1 = y1;
        sz0 = z1 + 1; sz1 = z1 + param;
        break;
    case SRD_FACE_NORTH: /* -Z: carve from (z0-param..z0-1) */
        sx0 = x0; sx1 = x1;
        sy0 = y0; sy1 = y1;
        sz0 = z0 - param; sz1 = z0 - 1;
        break;
    default:
        return -1;
    }

    /* Clamp to grid bounds */
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sz0 < 0) sz0 = 0;
    if (sx1 >= grid->nx) sx1 = grid->nx - 1;
    if (sy1 >= grid->ny) sy1 = grid->ny - 1;
    if (sz1 >= grid->nz) sz1 = grid->nz - 1;

    /* Carve the slice: make air, assign room ownership */
    for (int z = sz0; z <= sz1; z++) {
        for (int y = sy0; y <= sy1; y++) {
            for (int x = sx0; x <= sx1; x++) {
                int idx = z * ny * nx + y * nx + x;
                if (map->ids[idx] == 0) {
                    grid->values[idx] = -grid->voxel_size; /* negative = air */
                    map->ids[idx] = sel->room_id;
                }
            }
        }
    }

    return 0;
}
