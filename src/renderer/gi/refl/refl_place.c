/**
 * @file refl_place.c
 * @brief Sparse grid placement culled by the baked SDF (see refl_place.h).
 */
#include "ferrum/renderer/gi/refl_place.h"

#include <stddef.h>

#include "ferrum/renderer/gi/gi_sdf.h"

uint32_t refl_place_grid_fn(refl_probe_set_t *set, const float mn[3],
                            const float mx[3], float spacing,
                            refl_sdf_fn fn, void *user, float min_clear,
                            float near_max)
{
    if (set == NULL || set->probes == NULL || mn == NULL || mx == NULL ||
        spacing <= 0.0f)
        return 0u;
    int32_t n[3];
    for (int a = 0; a < 3; ++a) {
        float ext = mx[a] - mn[a];
        if (ext <= 0.0f)
            return 0u;
        n[a] = (int32_t)(ext / spacing);
        if (n[a] < 1)
            n[a] = 1;
    }
    uint32_t added = 0u;
    for (int32_t z = 0; z < n[2]; ++z)
        for (int32_t y = 0; y < n[1]; ++y)
            for (int32_t x = 0; x < n[0]; ++x) {
                if (set->count >= set->capacity)
                    return added;
                float p[3] = {
                    mn[0] + ((float)x + 0.5f) * (mx[0] - mn[0]) / (float)n[0],
                    mn[1] + ((float)y + 0.5f) * (mx[1] - mn[1]) / (float)n[1],
                    mn[2] + ((float)z + 0.5f) * (mx[2] - mn[2]) / (float)n[2],
                };
                if (fn != NULL) {
                    float d = fn(p, user);
                    if (d <= min_clear)
                        continue;      /* buried or hugging a wall. */
                    if (near_max > 0.0f && d > near_max)
                        continue;      /* open sky, no geometry in reach. */
                }
                refl_probe_t *pr = &set->probes[set->count];
                pr->pos[0] = p[0];
                pr->pos[1] = p[1];
                pr->pos[2] = p[2];
                pr->ao = 1.0f;
                pr->tile = set->count;
                set->count += 1u;
                added += 1u;
            }
    return added;
}

/* Grid-payload adapter mirroring refl_occl_cone's. */
struct refl_place_grid_payload {
    const float *dist;
    const int32_t *dims;
    const float *origin;
    float voxel;
};

static float place_grid_sample(const float p[3], void *user)
{
    const struct refl_place_grid_payload *g =
        (const struct refl_place_grid_payload *)user;
    return gi_sdf_baked_sample(g->dist, g->dims, g->origin, g->voxel, p);
}

uint32_t refl_place_grid(refl_probe_set_t *set, const float mn[3],
                         const float mx[3], float spacing,
                         const float *sdf_dist, const int32_t dims[3],
                         const float origin[3], float voxel,
                         float min_clear)
{
    if (sdf_dist == NULL || dims == NULL || origin == NULL)
        return refl_place_grid_fn(set, mn, mx, spacing, NULL, NULL,
                                  min_clear, 0.0f);
    struct refl_place_grid_payload g = { sdf_dist, dims, origin, voxel };
    return refl_place_grid_fn(set, mn, mx, spacing, place_grid_sample, &g,
                              min_clear, 0.0f);
}
