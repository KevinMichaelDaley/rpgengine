/**
 * @file srd_rules_vfeature.c
 * @brief Voxel feature rules: pillar add/remove, convert type.
 *
 * Non-static functions (3):
 *   srd_rule_add_pillar
 *   srd_rule_remove_pillar
 *   srd_rule_convert_type
 */
#include "ferrum/procgen/srd/srd_rules_vfeature.h"
#include "ferrum/procgen/srd/srd_room_type.h"

#include <math.h>

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

/* ── Add Pillar ───────────────────────────────────────────────── */

int srd_rule_add_pillar(srd_sdf_grid_t *grid, srd_room_map_t *map,
                        const srd_voxel_selection_t *sel) {
    if (validate_inputs(grid, map, sel) != 0) return -1;

    float radius = sel->param;
    if (radius <= 0.0f) return 0;

    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Pillar at XZ center of room */
    float cx = (float)(x0 + x1) * 0.5f;
    float cz = (float)(z0 + z1) * 0.5f;
    float r2 = radius * radius;

    /* Scan the room's XZ footprint */
    int scan_x0 = (int)(cx - radius) - 1;
    int scan_x1 = (int)(cx + radius) + 1;
    int scan_z0 = (int)(cz - radius) - 1;
    int scan_z1 = (int)(cz + radius) + 1;
    if (scan_x0 < x0) scan_x0 = x0;
    if (scan_x1 > x1) scan_x1 = x1;
    if (scan_z0 < z0) scan_z0 = z0;
    if (scan_z1 > z1) scan_z1 = z1;

    for (int z = scan_z0; z <= scan_z1; z++) {
        float dz = (float)z - cz;
        for (int x = scan_x0; x <= scan_x1; x++) {
            float dx = (float)x - cx;
            if (dx * dx + dz * dz > r2) continue;

            /* Fill entire Y column within room */
            for (int y = y0; y <= y1; y++) {
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

/* ── Remove Pillar ────────────────────────────────────────────── */

int srd_rule_remove_pillar(srd_sdf_grid_t *grid, srd_room_map_t *map,
                           const srd_voxel_selection_t *sel) {
    if (validate_inputs(grid, map, sel) != 0) return -1;

    float radius = sel->param;
    if (radius <= 0.0f) return 0;

    /* We need the room's bbox. But after add_pillar, the center voxels
     * are no longer owned by the room. Use the original room's footprint
     * by finding any remaining room voxels + the center gap. */
    int x0, y0, z0, x1, y1, z1;
    if (find_room_bbox(map, sel->room_id, &x0, &y0, &z0, &x1, &y1, &z1) != 0)
        return -1;

    int nx = grid->nx, ny = grid->ny;

    /* Pillar was at XZ center of room */
    float cx = (float)(x0 + x1) * 0.5f;
    float cz = (float)(z0 + z1) * 0.5f;
    float r2 = radius * radius;

    int scan_x0 = (int)(cx - radius) - 1;
    int scan_x1 = (int)(cx + radius) + 1;
    int scan_z0 = (int)(cz - radius) - 1;
    int scan_z1 = (int)(cz + radius) + 1;
    if (scan_x0 < 0) scan_x0 = 0;
    if (scan_x1 >= grid->nx) scan_x1 = grid->nx - 1;
    if (scan_z0 < 0) scan_z0 = 0;
    if (scan_z1 >= grid->nz) scan_z1 = grid->nz - 1;

    for (int z = scan_z0; z <= scan_z1; z++) {
        float dz = (float)z - cz;
        for (int x = scan_x0; x <= scan_x1; x++) {
            float dx = (float)x - cx;
            if (dx * dx + dz * dz > r2) continue;

            for (int y = y0; y <= y1; y++) {
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

/* ── Convert Type ─────────────────────────────────────────────── */

int srd_rule_convert_type(srd_sdf_grid_t *grid, srd_room_map_t *map,
                          const srd_voxel_selection_t *sel) {
    (void)grid; /* grid not modified by type change */
    if (!map || !map->ids || !sel) return -1;
    if (sel->room_id < 1 || sel->room_id > map->n_rooms) return -1;

    int step = (int)sel->param;
    int current = (int)srd_room_map_get_type(map, sel->room_id);
    int next = ((current + step) % SRD_ROOM_TYPE_COUNT + SRD_ROOM_TYPE_COUNT)
               % SRD_ROOM_TYPE_COUNT;

    srd_room_map_set_type(map, sel->room_id, (srd_room_type_t)next);
    return 0;
}
