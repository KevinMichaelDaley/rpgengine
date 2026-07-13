/**
 * @file lm_visibility.c
 * @brief 3D-DDA ray/segment visibility against the SVO (see lm_visibility.h).
 */
#include "ferrum/lightmap/lm_visibility.h"

#include <math.h>
#include <stddef.h>

/**
 * Clip the ray origin + s*dir (s in [0, maxdist]) to the grid's world bounds,
 * returning the entry/exit parameters in the out-params t0 and t1. Returns false if the ray
 * never intersects the box (slab test). *dir is assumed normalised.
 */
static bool lm_clip_to_bounds(const npc_svo_grid_t *svo, vec3_t origin,
                              vec3_t dir, float maxdist, float *t0, float *t1)
{
    const float o[3] = { origin.x, origin.y, origin.z };
    const float d[3] = { dir.x, dir.y, dir.z };
    const float lo[3] = { svo->world_bounds.min.x, svo->world_bounds.min.y,
                          svo->world_bounds.min.z };
    const float hi[3] = { svo->world_bounds.max.x, svo->world_bounds.max.y,
                          svo->world_bounds.max.z };
    float a = 0.0f, b = maxdist;
    for (int i = 0; i < 3; ++i) {
        if (fabsf(d[i]) < 1e-8f) {
            if (o[i] < lo[i] || o[i] > hi[i])
                return false; /* parallel to slab and outside it */
        } else {
            float inv = 1.0f / d[i];
            float ta = (lo[i] - o[i]) * inv;
            float tb = (hi[i] - o[i]) * inv;
            if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
            if (ta > a) a = ta;
            if (tb < b) b = tb;
            if (a > b) return false;
        }
    }
    *t0 = a;
    *t1 = b;
    return true;
}

/**
 * March the finest voxel grid (Amanatides & Woo 3D-DDA), querying occupancy at
 * each cell. Returns true on the first SOLID voxel within maxdist; if @p hit is
 * non-NULL it is filled for that voxel. @p dir must be normalised.
 */
static bool lm_dda(const npc_svo_grid_t *svo, vec3_t origin, vec3_t dir,
                   float maxdist, lm_ray_hit_t *hit)
{
    /* The SVO gives every axis 2^max_depth cells, so cell size is per-axis
     * (extent_i / cells) -- anisotropic for non-cubic bounds. Marching with a
     * single cubic voxel_size would sample the wrong cell centres and miss thin
     * geometry, so derive the true per-axis cell sizes here. */
    if (svo->max_depth == 0)
        return false;
    const float mn[3] = { svo->world_bounds.min.x, svo->world_bounds.min.y,
                          svo->world_bounds.min.z };
    const float mx[3] = { svo->world_bounds.max.x, svo->world_bounds.max.y,
                          svo->world_bounds.max.z };
    const float cells = (float)(1u << svo->max_depth);
    float cs[3];
    float min_cs = INFINITY;
    for (int i = 0; i < 3; ++i) {
        cs[i] = (mx[i] - mn[i]) / cells;
        if (cs[i] <= 0.0f)
            return false;
        if (cs[i] < min_cs)
            min_cs = cs[i];
    }

    float t0, t1;
    if (!lm_clip_to_bounds(svo, origin, dir, maxdist, &t0, &t1))
        return false;

    const float o[3] = { origin.x, origin.y, origin.z };
    const float d[3] = { dir.x, dir.y, dir.z };

    /* Enter a hair past t0 so we land inside the first cell, not on its face. */
    float start = t0 + min_cs * 1e-3f;
    int cell[3];
    int step[3];
    float tmax[3];
    float tdelta[3];
    for (int i = 0; i < 3; ++i) {
        float p = o[i] + d[i] * start;
        cell[i] = (int)floorf((p - mn[i]) / cs[i]);
        if (d[i] > 0.0f) {
            step[i] = 1;
            float next = mn[i] + (float)(cell[i] + 1) * cs[i];
            tmax[i] = (next - o[i]) / d[i];
            tdelta[i] = cs[i] / d[i];
        } else if (d[i] < 0.0f) {
            step[i] = -1;
            float next = mn[i] + (float)cell[i] * cs[i];
            tmax[i] = (next - o[i]) / d[i];
            tdelta[i] = cs[i] / -d[i];
        } else {
            step[i] = 0;
            tmax[i] = INFINITY;
            tdelta[i] = INFINITY;
        }
    }

    int last_axis = -1;
    float t = t0;
    /* Bound the walk: at most one cell per finest-axis cell of range. */
    long max_steps = (long)((t1 - t0) / min_cs) + 8;
    for (long s = 0; s < max_steps && t <= t1 + min_cs; ++s) {
        vec3_t centre = { mn[0] + ((float)cell[0] + 0.5f) * cs[0],
                          mn[1] + ((float)cell[1] + 0.5f) * cs[1],
                          mn[2] + ((float)cell[2] + 0.5f) * cs[2] };
        uint32_t node = NPC_SVO_INVALID_NODE;
        uint8_t flags = npc_svo_query_point(svo, centre, &node);
        if (flags & NPC_SVO_FLAG_SOLID) {
            if (hit) {
                hit->hit = true;
                hit->t = t;
                hit->position = vec3_add(origin, vec3_scale(dir, t));
                vec3_t n = { 0.0f, 0.0f, 0.0f };
                if (last_axis == 0) n.x = (float)(-step[0]);
                else if (last_axis == 1) n.y = (float)(-step[1]);
                else if (last_axis == 2) n.z = (float)(-step[2]);
                hit->normal = n;
                hit->material = (node != NPC_SVO_INVALID_NODE)
                                    ? svo->nodes[node].material
                                    : (uint16_t)0;
                hit->node = node;
            }
            return true;
        }
        /* Advance to the next voxel boundary along the closest axis. */
        int axis = 0;
        if (tmax[1] < tmax[0]) axis = 1;
        if (tmax[2] < tmax[axis]) axis = 2;
        t = tmax[axis];
        cell[axis] += step[axis];
        tmax[axis] += tdelta[axis];
        last_axis = axis;
    }
    return false;
}

bool lm_visibility_occluded(const npc_svo_grid_t *svo, vec3_t origin,
                            vec3_t dir, float maxdist)
{
    vec3_t nd = vec3_normalize_safe(dir, 1e-8f);
    if (nd.x == 0.0f && nd.y == 0.0f && nd.z == 0.0f)
        return false;
    return lm_dda(svo, origin, nd, maxdist, NULL);
}

bool lm_visibility_trace(const npc_svo_grid_t *svo, vec3_t origin,
                         vec3_t dir, float maxdist, lm_ray_hit_t *out)
{
    lm_ray_hit_t h = { 0.0f, { 0, 0, 0 }, { 0, 0, 0 }, 0, false,
                       NPC_SVO_INVALID_NODE };
    vec3_t nd = vec3_normalize_safe(dir, 1e-8f);
    bool hit = false;
    if (!(nd.x == 0.0f && nd.y == 0.0f && nd.z == 0.0f))
        hit = lm_dda(svo, origin, nd, maxdist, &h);
    if (out)
        *out = h;
    return hit;
}

bool lm_visibility_segment(const npc_svo_grid_t *svo, vec3_t p1, vec3_t p2)
{
    vec3_t seg = vec3_sub(p2, p1);
    float len = vec3_magnitude(seg);
    if (len < 1e-6f)
        return true;
    vec3_t dir = vec3_scale(seg, 1.0f / len);
    /* Bias both ends half a voxel inward so a surface point cannot occlude
     * itself or the target surface. */
    float bias = svo->voxel_size * 0.5f;
    if (len <= 2.0f * bias)
        return true;
    vec3_t origin = vec3_add(p1, vec3_scale(dir, bias));
    return !lm_dda(svo, origin, dir, len - 2.0f * bias, NULL);
}
