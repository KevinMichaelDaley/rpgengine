/**
 * @file srd_seed_init.c
 * @brief Convert grammar seed layout to voxel SDF grid + room map.
 *
 * Non-static functions (1): srd_seed_to_grid
 */
#include "ferrum/procgen/srd/srd_seed_init.h"

#include <math.h>

/* ── Helpers ───────────────────────────────────────────────────── */

/** @brief Compute the world-space bounding box of all seed rooms. */
static void compute_bounds(const srd_seed_room_t *rooms, int n,
                           float *min_x, float *min_y, float *min_z,
                           float *max_x, float *max_y, float *max_z) {
    *min_x = rooms[0].cx - rooms[0].hx;
    *min_y = rooms[0].cy - rooms[0].hy;
    *min_z = rooms[0].cz - rooms[0].hz;
    *max_x = rooms[0].cx + rooms[0].hx;
    *max_y = rooms[0].cy + rooms[0].hy;
    *max_z = rooms[0].cz + rooms[0].hz;

    for (int i = 1; i < n; i++) {
        float lo_x = rooms[i].cx - rooms[i].hx;
        float lo_y = rooms[i].cy - rooms[i].hy;
        float lo_z = rooms[i].cz - rooms[i].hz;
        float hi_x = rooms[i].cx + rooms[i].hx;
        float hi_y = rooms[i].cy + rooms[i].hy;
        float hi_z = rooms[i].cz + rooms[i].hz;
        if (lo_x < *min_x) *min_x = lo_x;
        if (lo_y < *min_y) *min_y = lo_y;
        if (lo_z < *min_z) *min_z = lo_z;
        if (hi_x > *max_x) *max_x = hi_x;
        if (hi_y > *max_y) *max_y = hi_y;
        if (hi_z > *max_z) *max_z = hi_z;
    }
}

/**
 * @brief Carve a doorway between two adjacent rooms.
 *
 * Finds the closest axis between rooms a and b, computes the shared
 * face location, and stamps a thin box at that interface to connect
 * the rooms.
 */
static void carve_doorway(srd_sdf_grid_t *grid,
                          const srd_seed_room_t *a,
                          const srd_seed_room_t *b) {
    /* Compute separation along each axis */
    float dx = fabsf(a->cx - b->cx) - (a->hx + b->hx);
    float dy = fabsf(a->cy - b->cy) - (a->hy + b->hy);
    float dz = fabsf(a->cz - b->cz) - (a->hz + b->hz);

    /* Find the axis with the smallest gap (most likely the connecting face).
     * Negative gap means overlap; near-zero means touching. */
    float gap_x = dx;
    float gap_y = dy;
    float gap_z = dz;

    /* Doorway dimensions */
    float door_width = 0.5f;  /* Half-width of doorway perpendicular to connecting axis */
    float door_depth = 1.5f;  /* Penetration depth into each room — must be >voxel_size
                                * to create negative SDF values past the boundary */

    /* Midpoint between rooms */
    float mid_x = (a->cx + b->cx) * 0.5f;
    float mid_y = (a->cy + b->cy) * 0.5f;
    float mid_z = (a->cz + b->cz) * 0.5f;

    /* Use the minimum height of the two rooms for the doorway */
    float min_hy = (a->hy < b->hy) ? a->hy : b->hy;
    /* Doorway height: 80% of the smaller room's height */
    float door_hy = min_hy * 0.8f;

    if (gap_x >= gap_y && gap_x >= gap_z) {
        /* Rooms separated along X — carve doorway through X wall */
        float half_gap = fabsf(a->cx - b->cx) * 0.5f;
        float door_hx = half_gap - ((a->hx < b->hx) ? b->hx : a->hx) + door_depth;
        if (door_hx < door_depth) door_hx = door_depth;

        /* Clamp perpendicular size to fit within both rooms */
        float min_hz = (a->hz < b->hz) ? a->hz : b->hz;
        float dw = (door_width < min_hz) ? door_width : min_hz;

        srd_sdf_grid_stamp_box(grid, mid_x, mid_y, mid_z,
                               door_hx, door_hy, dw);
    } else if (gap_z >= gap_x && gap_z >= gap_y) {
        /* Rooms separated along Z — carve doorway through Z wall */
        float half_gap = fabsf(a->cz - b->cz) * 0.5f;
        float door_hz = half_gap - ((a->hz < b->hz) ? b->hz : a->hz) + door_depth;
        if (door_hz < door_depth) door_hz = door_depth;

        float min_hx = (a->hx < b->hx) ? a->hx : b->hx;
        float dw = (door_width < min_hx) ? door_width : min_hx;

        srd_sdf_grid_stamp_box(grid, mid_x, mid_y, mid_z,
                               dw, door_hy, door_hz);
    } else {
        /* Rooms separated along Y — carve doorway through floor/ceiling */
        float half_gap = fabsf(a->cy - b->cy) * 0.5f;
        float door_hy2 = half_gap - ((a->hy < b->hy) ? b->hy : a->hy) + door_depth;
        if (door_hy2 < door_depth) door_hy2 = door_depth;

        float min_hx = (a->hx < b->hx) ? a->hx : b->hx;
        float min_hz = (a->hz < b->hz) ? a->hz : b->hz;
        float dw_x = (door_width < min_hx) ? door_width : min_hx;
        float dw_z = (door_width < min_hz) ? door_width : min_hz;

        srd_sdf_grid_stamp_box(grid, mid_x, mid_y, mid_z,
                               dw_x, door_hy2, dw_z);
    }
}

/* ── Public API ────────────────────────────────────────────────── */

int srd_seed_to_grid(const srd_seed_room_t *rooms, int n_rooms,
                     const int *adj_pairs, int n_pairs,
                     float voxel_size, float margin,
                     srd_sdf_grid_t *grid_out,
                     srd_room_map_t *map_out) {
    if (!rooms || n_rooms <= 0 || !grid_out || !map_out)
        return -1;
    if (voxel_size <= 0.0f)
        return -1;

    /* Step 1: Compute bounding box of all rooms */
    float min_x, min_y, min_z, max_x, max_y, max_z;
    compute_bounds(rooms, n_rooms, &min_x, &min_y, &min_z,
                   &max_x, &max_y, &max_z);

    /* Add margin */
    min_x -= margin;
    min_y -= margin;
    min_z -= margin;
    max_x += margin;
    max_y += margin;
    max_z += margin;

    /* Step 2: Compute grid dimensions */
    int nx = (int)ceilf((max_x - min_x) / voxel_size);
    int ny = (int)ceilf((max_y - min_y) / voxel_size);
    int nz = (int)ceilf((max_z - min_z) / voxel_size);
    if (nx < 1) nx = 1;
    if (ny < 1) ny = 1;
    if (nz < 1) nz = 1;

    /* Step 3: Initialize grid and room map */
    float origin[3] = {min_x, min_y, min_z};
    int rc = srd_sdf_grid_init(grid_out, nx, ny, nz, voxel_size, origin);
    if (rc != 0) return -1;

    rc = srd_room_map_init(map_out, nx, ny, nz);
    if (rc != 0) {
        srd_sdf_grid_destroy(grid_out);
        return -1;
    }

    /* Step 4: Stamp each room into the grid and assign room IDs */
    for (int i = 0; i < n_rooms; i++) {
        const srd_seed_room_t *r = &rooms[i];

        /* Carve the room into the SDF grid */
        srd_sdf_grid_stamp_box(grid_out,
                               r->cx, r->cy, r->cz,
                               r->hx, r->hy, r->hz);

        /* Add room to the map and assign IDs to carved voxels */
        uint8_t room_id = srd_room_map_add_room(map_out, r->type);
        if (room_id == 0) {
            srd_sdf_grid_destroy(grid_out);
            srd_room_map_destroy(map_out);
            return -1;
        }
        srd_room_map_stamp_from_sdf(map_out, grid_out, room_id);
    }

    /* Step 5: Carve doorways for each adjacency pair */
    if (adj_pairs && n_pairs > 0) {
        for (int p = 0; p < n_pairs; p++) {
            int a = adj_pairs[p * 2 + 0];
            int b = adj_pairs[p * 2 + 1];
            if (a < 0 || a >= n_rooms || b < 0 || b >= n_rooms)
                continue;

            carve_doorway(grid_out, &rooms[a], &rooms[b]);

            /* Set adjacency in room map (1-based IDs) */
            srd_room_map_set_adjacent(map_out, (uint8_t)(a + 1),
                                      (uint8_t)(b + 1), true);
        }
    }

    return 0;
}
