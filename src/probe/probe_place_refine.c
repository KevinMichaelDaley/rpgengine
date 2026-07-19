/**
 * @file probe_place_refine.c
 * @brief Importance-box densification of a base probe set (rpg-ft0g): finer
 *        probes inside high-density regions => distance/LOD resolution.
 */
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/probe_place.h"

/* Clip [box_min,box_max] to [amin,amax] into lo/hi; returns false if empty. */
static bool clip_box(const float *box_min, const float *box_max,
                     const float *amin, const float *amax, float lo[3], float hi[3])
{
    for (int a = 0; a < 3; ++a) {
        lo[a] = box_min[a] > amin[a] ? box_min[a] : amin[a];
        hi[a] = box_max[a] < amax[a] ? box_max[a] : amax[a];
        if (hi[a] <= lo[a]) return false;
    }
    return true;
}

/* Per-axis fine sub-lattice counts for a clipped box at spacing fs[]. */
static void sub_counts(const float lo[3], const float hi[3], const float fs[3],
                       int n[3])
{
    for (int a = 0; a < 3; ++a) {
        int c = (int)((hi[a] - lo[a]) / fs[a]) + 1;
        n[a] = c < 1 ? 1 : c;
    }
}

/* Base cell size to subdivide from: the grid cell if base is a grid, else the
 * descriptor spacing (uniform). */
static void base_cell(const probe_set_t *base, const scene_desc_probes_t *spec,
                      float bc[3])
{
    if (base->grid_dim[0] > 0) {
        bc[0] = base->grid_cell[0]; bc[1] = base->grid_cell[1]; bc[2] = base->grid_cell[2];
    } else {
        float sp = (spec != NULL && spec->spacing > 0.0f) ? spec->spacing : 2.0f;
        bc[0] = bc[1] = bc[2] = sp;
    }
}

bool probe_place_refine_importance(const probe_set_t *base,
                                   const scene_desc_probes_t *spec,
                                   const float aabb_min[3],
                                   const float aabb_max[3], struct arena *arena,
                                   probe_set_t *out)
{
    if (base == NULL || aabb_min == NULL || aabb_max == NULL || arena == NULL ||
        out == NULL)
        return false;
    memset(out, 0, sizeof *out);

    float bc[3];
    base_cell(base, spec, bc);

    /* Pass 1: count the extra probes across all densifying boxes. */
    uint32_t extra = 0;
    uint32_t nbox = (spec != NULL) ? spec->box_count : 0u;
    for (uint32_t b = 0; b < nbox; ++b) {
        const scene_desc_importance_box_t *box = &spec->boxes[b];
        if (box->density_mult <= 1.0f) continue;
        float lo[3], hi[3];
        if (!clip_box(box->min, box->max, aabb_min, aabb_max, lo, hi)) continue;
        float fs[3]; int n[3];
        for (int a = 0; a < 3; ++a) {
            fs[a] = bc[a] / box->density_mult;
            if (fs[a] < 0.05f) fs[a] = 0.05f;   /* guard degenerate density. */
        }
        sub_counts(lo, hi, fs, n);
        extra += (uint32_t)n[0] * (uint32_t)n[1] * (uint32_t)n[2];
    }

    uint32_t total = base->count + extra;
    float *pos = arena_alloc((arena_t *)arena, 16u, (size_t)total * 3u * sizeof(float));
    if (pos == NULL) return false;

    /* Copy all base probes first (they are always kept). */
    if (base->count > 0 && base->positions != NULL)
        memcpy(pos, base->positions, (size_t)base->count * 3u * sizeof(float));

    /* Pass 2: emit the fine sub-lattices. */
    uint32_t k = base->count;
    for (uint32_t b = 0; b < nbox; ++b) {
        const scene_desc_importance_box_t *box = &spec->boxes[b];
        if (box->density_mult <= 1.0f) continue;
        float lo[3], hi[3];
        if (!clip_box(box->min, box->max, aabb_min, aabb_max, lo, hi)) continue;
        float fs[3]; int n[3];
        for (int a = 0; a < 3; ++a) {
            fs[a] = bc[a] / box->density_mult;
            if (fs[a] < 0.05f) fs[a] = 0.05f;
        }
        sub_counts(lo, hi, fs, n);
        for (int iz = 0; iz < n[2]; ++iz)
        for (int iy = 0; iy < n[1]; ++iy)
        for (int ix = 0; ix < n[0]; ++ix) {
            pos[k * 3 + 0] = lo[0] + fs[0] * (float)ix;
            pos[k * 3 + 1] = lo[1] + fs[1] * (float)iy;
            pos[k * 3 + 2] = lo[2] + fs[2] * (float)iz;
            ++k;
        }
    }

    out->count = total;
    out->positions = pos;
    /* Non-uniform result -> unstructured point set (grid_dim stays 0). */
    return true;
}
