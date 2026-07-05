#include "ferrum/procgen/srd/srd_loss_primitives.h"
#include "ferrum/procgen/srd/srd_eikonal.h"
#include "ferrum/procgen/srd/srd_transport.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

/* ── Helpers ──────────────────────────────────────────────────── */

static void build_occupancy_grid(const fr_room_box_t *rooms, uint32_t n_rooms,
                                  int nx, int nz, double *occ,
                                  float world_x0, float world_z0,
                                  float cell_w) {
    (void)rooms; (void)n_rooms;
    /* Default: all empty space (rooms are traversable, no walls) */
    memset(occ, 0, nx * nz * sizeof(double));
}

static void world_to_grid(const fr_room_box_t *room,
                          float world_x0, float world_z0, float cell_w,
                          float *gx, float *gz) {
    *gx = (room->center_x - world_x0) / cell_w;
    *gz = (room->center_z - world_z0) / cell_w;
}

/* ── Primitives ───────────────────────────────────────────────── */

double srd_loss_path_distance(const fr_room_box_t *rooms, uint32_t n_rooms,
                               uint32_t src_idx, uint32_t tgt_idx,
                               int grid_nx, int grid_nz) {
    if (!rooms || src_idx >= n_rooms || tgt_idx >= n_rooms) return 0.0;
    const fr_room_box_t *src = &rooms[src_idx];
    const fr_room_box_t *tgt = &rooms[tgt_idx];

    float cell_w = 1.0f;
    float mid_x = (src->center_x + tgt->center_x) * 0.5f;
    float mid_z = (src->center_z + tgt->center_z) * 0.5f;
    float world_x0 = mid_x - (float)grid_nx * cell_w * 0.5f;
    float world_z0 = mid_z - (float)grid_nz * cell_w * 0.5f;

    double *occ = new double[grid_nx * grid_nz];
    double *T   = new double[grid_nx * grid_nz];

    build_occupancy_grid(rooms, n_rooms, grid_nx, grid_nz, occ, world_x0, world_z0, cell_w);

    float gx_src, gz_src, gx_tgt, gz_tgt;
    world_to_grid(src, world_x0, world_z0, cell_w, &gx_src, &gz_src);
    world_to_grid(tgt, world_x0, world_z0, cell_w, &gx_tgt, &gz_tgt);

    int isx = (int)gx_src, isz = (int)gz_src;
    if (isx < 0) isx = 0; if (isx >= grid_nx) isx = grid_nx - 1;
    if (isz < 0) isz = 0; if (isz >= grid_nz) isz = grid_nz - 1;

    double t_val = srd_eikonal_solve_2d(grid_nx, grid_nz, occ, isx, isz, T);
    if (t_val > 1e10) { delete[] occ; delete[] T; return 1e6; }

    int itx = (int)gx_tgt, itz = (int)gz_tgt;
    if (itx < 0) itx = 0; if (itx >= grid_nx) itx = grid_nx - 1;
    if (itz < 0) itz = 0; if (itz >= grid_nz) itz = grid_nz - 1;

    double d = T[itz * grid_nx + itx];
    delete[] occ;
    delete[] T;
    return d;
}

double srd_loss_line_of_sight(const fr_room_box_t *rooms, uint32_t n_rooms,
                               uint32_t src_idx, uint32_t tgt_idx,
                               int grid_nx, int grid_nz) {
    if (!rooms || src_idx >= n_rooms || tgt_idx >= n_rooms) return 0.0;
    const fr_room_box_t *src = &rooms[src_idx];
    const fr_room_box_t *tgt = &rooms[tgt_idx];

    float cell_w = 1.0f;
    float mid_x = (src->center_x + tgt->center_x) * 0.5f;
    float mid_z = (src->center_z + tgt->center_z) * 0.5f;
    float world_x0 = mid_x - (float)grid_nx * cell_w * 0.5f;
    float world_z0 = mid_z - (float)grid_nz * cell_w * 0.5f;

    double *occ = new double[grid_nx * grid_nz];
    double *R   = new double[grid_nx * grid_nz];
    build_occupancy_grid(rooms, n_rooms, grid_nx, grid_nz, occ, world_x0, world_z0, cell_w);

    float gx_src, gz_src, gx_tgt, gz_tgt;
    world_to_grid(src, world_x0, world_z0, cell_w, &gx_src, &gz_src);
    world_to_grid(tgt, world_x0, world_z0, cell_w, &gx_tgt, &gz_tgt);

    int isx = (int)gx_src, isz = (int)gz_src;
    int itx = (int)gx_tgt, itz = (int)gz_tgt;
    if (isx < 0) isx = 0; if (isx >= grid_nx) isx = grid_nx - 1;
    if (isz < 0) isz = 0; if (isz >= grid_nz) isz = grid_nz - 1;
    if (itx < 0) itx = 0; if (itx >= grid_nx) itx = grid_nx - 1;
    if (itz < 0) itz = 0; if (itz >= grid_nz) itz = grid_nz - 1;

    double r_val = srd_transport_solve_2d(grid_nx, grid_nz, occ, isx, isz, itx, itz, R);
    delete[] occ;
    delete[] R;
    return 1.0 - r_val;  /* loss = 1 - visibility */
}

double srd_loss_non_penetration(const fr_room_box_t *rooms, uint32_t n_rooms) {
    double loss = 0.0;
    for (uint32_t i = 0; i < n_rooms; i++) {
        for (uint32_t j = i + 1; j < n_rooms; j++) {
            float dx = rooms[i].center_x - rooms[j].center_x;
            float dz = rooms[i].center_z - rooms[j].center_z;
            float dist2 = dx*dx + dz*dz;
            float sigma2 = (rooms[i].half_extent_x + rooms[j].half_extent_x)
                         * (rooms[i].half_extent_x + rooms[j].half_extent_x)
                         + (rooms[i].half_extent_z + rooms[j].half_extent_z)
                         * (rooms[i].half_extent_z + rooms[j].half_extent_z);
            loss += expf(-dist2 / (sigma2 + 1e-6f));
        }
    }
    return loss;
}

double srd_loss_minimum_size(const fr_room_box_t *room, double min_half) {
    double loss = 0.0;
    if (room->half_extent_x < min_half)
        loss += (min_half - room->half_extent_x) * (min_half - room->half_extent_x);
    if (room->half_extent_z < min_half)
        loss += (min_half - room->half_extent_z) * (min_half - room->half_extent_z);
    return loss;
}

double srd_loss_separation(const fr_room_box_t *a, const fr_room_box_t *b,
                            double target_dist, int op) {
    float dx = a->center_x - b->center_x;
    float dz = a->center_z - b->center_z;
    float dist = sqrtf(dx*dx + dz*dz);
    if (op == 0) /* want > target */
        return dist < target_dist ? (target_dist - dist) : 0.0;
    else /* want < target */
        return dist > target_dist ? (dist - target_dist) : 0.0;
}

double srd_loss_height_span(const fr_room_box_t *room,
                             double min_height, double max_height) {
    float h = room->ceil_z - room->floor_z;
    double loss = 0.0;
    if (h < min_height) loss += (min_height - h) * (min_height - h);
    if (h > max_height) loss += (h - max_height) * (h - max_height);
    return loss;
}

double srd_loss_stair_alignment(double ax, double az, double tx, double tz) {
    double dx = ax - tx, dz = az - tz;
    return dx*dx + dz*dz;
}
