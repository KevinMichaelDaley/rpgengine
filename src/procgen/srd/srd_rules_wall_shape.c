/**
 * @file srd_rules_wall_shape.c
 * @brief Wall bevel and niche rewrite rules.
 *
 * Non-static functions (2):
 *   srd_rule_wall_bevel
 *   srd_rule_wall_niche
 */
#include "ferrum/procgen/srd/srd_rules_wall.h"

/* ── Helpers ──────────────────────────────────────────────────── */

/**
 * @brief Validate common wall rule inputs.
 */
static int validate_inputs(const srd_sdf_grid_t *grid,
                           const srd_room_map_t *map,
                           const srd_voxel_selection_t *sel) {
    if (!grid || !grid->values || !map || !map->ids || !sel) return -1;
    if (sel->room_id < 1 || sel->room_id > map->n_rooms) return -1;
    if (sel->face < SRD_FACE_NORTH || sel->face > SRD_FACE_WEST) return -1;
    return 0;
}

/**
 * @brief Find the axis-aligned bounding box of a room in voxel coords.
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

/* ── Bevel ────────────────────────────────────────────────────── */

int srd_rule_wall_bevel(srd_sdf_grid_t *grid, srd_room_map_t *map,
                        const srd_voxel_selection_t *sel) {
    if (validate_inputs(grid, map, sel) != 0) return -1;

    int bevel = (int)sel->param;
    if (bevel <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Bevel carves a 45-degree chamfer at the wall-ceiling edge.
     * For each position along the face, carve a triangular region
     * where distance_from_wall + distance_from_ceiling < bevel.
     *
     * For EAST face: wall is at x=x1+1 (first solid column beyond room).
     * Ceiling is at y=y1+1.
     * Carve voxels where (x - x1) + (y1 + 1 - y) <= bevel
     *   → x >= x1+1, y <= y1, and sum of offsets <= bevel. */

    /* Determine the wall position and ceiling/floor for the bevel */
    int wall_pos, wall_dir; /* wall_pos = first solid voxel index, wall_dir = axis */
    int ceil_y = y1 + 1;    /* first solid voxel above room */

    /* Range along the face's lateral axes */
    int la0, la1; /* lateral axis range (the axis perpendicular to both face normal and Y) */

    switch (sel->face) {
    case SRD_FACE_EAST:
        wall_pos = x1 + 1;
        wall_dir = 0; /* X axis */
        la0 = z0; la1 = z1;
        break;
    case SRD_FACE_WEST:
        wall_pos = x0 - 1;
        wall_dir = 1; /* X axis, negative direction */
        la0 = z0; la1 = z1;
        break;
    case SRD_FACE_SOUTH:
        wall_pos = z1 + 1;
        wall_dir = 2; /* Z axis */
        la0 = x0; la1 = x1;
        break;
    case SRD_FACE_NORTH:
        wall_pos = z0 - 1;
        wall_dir = 3; /* Z axis, negative direction */
        la0 = x0; la1 = x1;
        break;
    default:
        return -1;
    }

    /* Carve the triangular bevel region */
    for (int d_wall = 0; d_wall < bevel; d_wall++) {
        int d_ceil_max = bevel - d_wall;
        for (int d_ceil = 0; d_ceil < d_ceil_max; d_ceil++) {
            int y = ceil_y - 1 - d_ceil; /* going down from ceiling */
            if (y < 0 || y >= grid->ny) continue;

            for (int la = la0; la <= la1; la++) {
                int x, z;
                switch (wall_dir) {
                case 0: /* East: wall extends in +X */
                    x = wall_pos + d_wall;
                    z = la;
                    break;
                case 1: /* West: wall extends in -X */
                    x = wall_pos - d_wall;
                    z = la;
                    break;
                case 2: /* South: wall extends in +Z */
                    z = wall_pos + d_wall;
                    x = la;
                    break;
                case 3: /* North: wall extends in -Z */
                    z = wall_pos - d_wall;
                    x = la;
                    break;
                default:
                    continue;
                }

                if (x < 0 || x >= grid->nx || z < 0 || z >= grid->nz) continue;

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

/* ── Niche ────────────────────────────────────────────────────── */

int srd_rule_wall_niche(srd_sdf_grid_t *grid, srd_room_map_t *map,
                        const srd_voxel_selection_t *sel) {
    if (validate_inputs(grid, map, sel) != 0) return -1;

    int depth = (int)sel->param;
    if (depth <= 0) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Niche is a rectangular alcove carved into the wall.
     * Centered on the face, half the face's width and height.
     *
     * For EAST face:
     *   - Extends from x=x1+1 to x=x1+depth (into the wall)
     *   - Centered in Y: from y_mid - h/4 to y_mid + h/4
     *   - Centered in Z: from z_mid - w/4 to z_mid + w/4 */

    int room_h = y1 - y0 + 1;
    int y_mid = (y0 + y1) / 2;
    int niche_h_half = room_h / 4;
    if (niche_h_half < 1) niche_h_half = 1;
    int ny0 = y_mid - niche_h_half;
    int ny1 = y_mid + niche_h_half;

    /* Compute niche extents based on face */
    int sx0, sx1, sz0, sz1;
    switch (sel->face) {
    case SRD_FACE_EAST: {
        sx0 = x1 + 1; sx1 = x1 + depth;
        int room_w = z1 - z0 + 1;
        int z_mid = (z0 + z1) / 2;
        int niche_w_half = room_w / 4;
        if (niche_w_half < 1) niche_w_half = 1;
        sz0 = z_mid - niche_w_half;
        sz1 = z_mid + niche_w_half;
        break;
    }
    case SRD_FACE_WEST: {
        sx0 = x0 - depth; sx1 = x0 - 1;
        int room_w = z1 - z0 + 1;
        int z_mid = (z0 + z1) / 2;
        int niche_w_half = room_w / 4;
        if (niche_w_half < 1) niche_w_half = 1;
        sz0 = z_mid - niche_w_half;
        sz1 = z_mid + niche_w_half;
        break;
    }
    case SRD_FACE_SOUTH: {
        sz0 = z1 + 1; sz1 = z1 + depth;
        int room_w = x1 - x0 + 1;
        int x_mid = (x0 + x1) / 2;
        int niche_w_half = room_w / 4;
        if (niche_w_half < 1) niche_w_half = 1;
        sx0 = x_mid - niche_w_half;
        sx1 = x_mid + niche_w_half;
        break;
    }
    case SRD_FACE_NORTH: {
        sz0 = z0 - depth; sz1 = z0 - 1;
        int room_w = x1 - x0 + 1;
        int x_mid = (x0 + x1) / 2;
        int niche_w_half = room_w / 4;
        if (niche_w_half < 1) niche_w_half = 1;
        sx0 = x_mid - niche_w_half;
        sx1 = x_mid + niche_w_half;
        break;
    }
    default:
        return -1;
    }

    /* Clamp to grid bounds */
    if (sx0 < 0) sx0 = 0;
    if (ny0 < 0) ny0 = 0;
    if (sz0 < 0) sz0 = 0;
    if (sx1 >= grid->nx) sx1 = grid->nx - 1;
    if (ny1 >= grid->ny) ny1 = grid->ny - 1;
    if (sz1 >= grid->nz) sz1 = grid->nz - 1;

    /* Carve the niche */
    for (int z = sz0; z <= sz1; z++) {
        for (int y = ny0; y <= ny1; y++) {
            for (int x = sx0; x <= sx1; x++) {
                if (x < 0 || x >= grid->nx) continue;
                if (y < 0 || y >= grid->ny) continue;
                if (z < 0 || z >= grid->nz) continue;
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
