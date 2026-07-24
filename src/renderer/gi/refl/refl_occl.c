/**
 * @file refl_occl.c
 * @brief SDF specular-occlusion cone march (see refl_occl.h).
 */
#include "ferrum/renderer/gi/refl_occl.h"

#include <stddef.h>

#include "ferrum/renderer/gi/gi_sdf.h"

float refl_occl_cone_fn(refl_sdf_fn fn, void *user, const float p[3],
                        const float dir[3], float cone_tan, float max_dist)
{
    if (fn == NULL || p == NULL || dir == NULL)
        return 1.0f;
    if (cone_tan < 0.02f)
        cone_tan = 0.02f;
    if (max_dist <= 0.0f)
        return 1.0f;
    float step_floor = 0.05f;
    float vis = 1.0f;
    float t = step_floor;               /* skip the seed point. */
    for (int step = 0; step < 96 && t < max_dist; ++step) {
        float q[3] = { p[0] + dir[0] * t, p[1] + dir[1] * t,
                       p[2] + dir[2] * t };
        float d = fn(q, user);
        if (d < 0.0f)
            return 0.0f;                /* inside a solid. */
        float v = d / (cone_tan * t);
        if (v < vis)
            vis = v;
        if (vis <= 0.005f)
            return 0.0f;
        float adv = d;                  /* sphere-trace, floored. */
        if (adv < step_floor)
            adv = step_floor;
        t += adv;
    }
    return (vis < 1.0f) ? ((vis > 0.0f) ? vis : 0.0f) : 1.0f;
}

/* Grid-payload adapter for the callback path. */
struct refl_occl_grid {
    const float *dist;
    const int32_t *dims;
    const float *origin;
    float voxel;
};

static float grid_sample(const float p[3], void *user)
{
    const struct refl_occl_grid *g = (const struct refl_occl_grid *)user;
    return gi_sdf_baked_sample(g->dist, g->dims, g->origin, g->voxel, p);
}

float refl_occl_cone(const float *dist, const int32_t dims[3],
                     const float origin[3], float voxel, const float p[3],
                     const float dir[3], float cone_tan, float max_dist)
{
    if (dist == NULL || dims == NULL || origin == NULL)
        return 1.0f;
    struct refl_occl_grid g = { dist, dims, origin, voxel };
    return refl_occl_cone_fn(grid_sample, &g, p, dir, cone_tan, max_dist);
}
