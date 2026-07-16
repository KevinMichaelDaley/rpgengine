/**
 * @file gi_probe_place.c
 * @brief Probe placement seeding (see gi_probe_place.h).
 */
#include "ferrum/renderer/gi/gi_probe_place.h"

#include <stddef.h>

uint32_t gi_probe_seed_box(gi_probe_set_t *set, const float aabb_min[3],
                           const float aabb_max[3], float spacing)
{
    if (set == NULL || aabb_min == NULL || aabb_max == NULL || spacing <= 0.0f)
        return 0u;

    int32_t n[3];
    for (int a = 0; a < 3; ++a) {
        float span = aabb_max[a] - aabb_min[a];
        int32_t c = (int32_t)(span / spacing);
        n[a] = c < 1 ? 1 : c;
    }

    uint32_t added = 0u;
    for (int32_t iz = 0; iz < n[2]; ++iz)
    for (int32_t iy = 0; iy < n[1]; ++iy)
    for (int32_t ix = 0; ix < n[0]; ++ix) {
        float x = aabb_min[0] + ((float)ix + 0.5f) * spacing;
        float y = aabb_min[1] + ((float)iy + 0.5f) * spacing;
        float z = aabb_min[2] + ((float)iz + 0.5f) * spacing;
        if (gi_probe_add(set, x, y, z) < 0)
            return added;               /* set full. */
        ++added;
    }
    return added;
}
