/**
 * @file sphere_cull.c
 * @brief AABB-vs-sphere culling (rpg-9u96). See sphere_cull_aabb in frustum_cull.h.
 *
 * Used to skip shadow casters outside a point light's range: a renderable whose
 * world AABB does not reach within @p radius of the light centre contributes
 * nothing to that light's cube shadow and can be dropped.
 */
#include "ferrum/renderer/cull/frustum_cull.h"

#include "ferrum/renderer/cull/cull_internal.h"

int sphere_cull_aabb(const float center[3], float radius, const float model[16],
                     const float lmin[3], const float lmax[3])
{
    if (radius <= 0.0f)
        return 0;                    /* unbounded range -> never cull. */
    float wmin[3], wmax[3];
    cull_world_aabb(model, lmin, lmax, wmin, wmax);
    return cull_aabb_point_dist2(wmin, wmax, center) > radius * radius;
}
