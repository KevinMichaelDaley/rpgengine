/**
 * @file probe_brick_index_build.c
 * @brief Voxel -> brick lookup construction (see probe_brick_index.h).
 *
 * Rasterizes each brick's voxel box into the dense grid in input order. The
 * placer emits ancestors before descendants (DFS), and only ancestor chains
 * overlap, so in-order splatting leaves the FINEST covering brick in every
 * voxel -- no sorting, no level comparisons.
 */
#include <math.h>
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/place/probe_brick_index.h"

bool probe_brick_index_build(const probe_brick_config_t *cfg,
                             const probe_brick_t *bricks, uint32_t n_bricks,
                             struct arena *arena, probe_brick_index_t *out)
{
    if (cfg == NULL || arena == NULL || out == NULL || cfg->coarse_brick <= 0.0f ||
        cfg->levels < 1 || cfg->levels > PROBE_BRICK_MAX_LEVELS ||
        (bricks == NULL && n_bricks > 0))
        return false;

    /* Finest brick edge = voxel size; the grid covers the coarse-cell cover of
     * the AABB (bricks may overhang aabb_max exactly as the placer's cells do). */
    float voxel = cfg->coarse_brick;
    for (int l = 1; l < cfg->levels; ++l) voxel /= 3.0f;
    int32_t fine_per_coarse = 1;
    for (int l = 1; l < cfg->levels; ++l) fine_per_coarse *= 3;

    probe_brick_index_t ix;
    memset(&ix, 0, sizeof ix);
    ix.voxel = voxel;
    size_t n_vox = 1;
    for (int a = 0; a < 3; ++a) {
        float span = cfg->aabb_max[a] - cfg->aabb_min[a];
        int32_t n_coarse = (int32_t)ceilf(span / cfg->coarse_brick);
        if (n_coarse < 1) n_coarse = 1;
        ix.dim[a] = n_coarse * fine_per_coarse;
        ix.origin[a] = cfg->aabb_min[a];
        n_vox *= (size_t)ix.dim[a];
    }

    ix.brick_of = arena_alloc((arena_t *)arena, 16u, n_vox * sizeof(int32_t));
    if (ix.brick_of == NULL) return false;
    memset(ix.brick_of, 0xff, n_vox * sizeof(int32_t));   /* all -1. */

    for (uint32_t b = 0; b < n_bricks; ++b) {
        /* The brick's voxel box: size/voxel is an exact power of 3, and brick
         * corners lie on the voxel lattice; round to defeat float error. */
        int32_t lo[3], span_v = (int32_t)lroundf(bricks[b].size / voxel);
        if (span_v < 1) span_v = 1;
        for (int a = 0; a < 3; ++a)
            lo[a] = (int32_t)lroundf((bricks[b].min[a] - ix.origin[a]) / voxel);
        for (int32_t z = 0; z < span_v; ++z)
            for (int32_t y = 0; y < span_v; ++y)
                for (int32_t x = 0; x < span_v; ++x) {
                    int32_t vx = lo[0] + x, vy = lo[1] + y, vz = lo[2] + z;
                    if (vx < 0 || vy < 0 || vz < 0 || vx >= ix.dim[0] ||
                        vy >= ix.dim[1] || vz >= ix.dim[2])
                        continue;
                    ix.brick_of[((size_t)vz * (size_t)ix.dim[1] + (size_t)vy) *
                                (size_t)ix.dim[0] + (size_t)vx] = (int32_t)b;
                }
    }

    *out = ix;
    return true;
}
