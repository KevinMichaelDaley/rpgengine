/**
 * @file convex_hull_support.c
 * @brief Support function and bounds computation for convex hulls.
 *
 * Non-static functions (3):
 *   1. phys_convex_hull_support
 *   2. phys_convex_hull_world_aabb
 *   3. phys_convex_hull_recompute_bounds
 */

#include "ferrum/physics/convex_hull.h"

#include <float.h>

#include "ferrum/math/quat.h"

phys_vec3_t phys_convex_hull_support(const phys_convex_hull_t *hull,
                                     phys_vec3_t dir) {
    if (!hull || hull->vertex_count == 0) {
        return (phys_vec3_t){0, 0, 0};
    }

    float best_dot = -FLT_MAX;
    uint32_t best_idx = 0;

    for (uint32_t i = 0; i < hull->vertex_count; i++) {
        float d = vec3_dot(hull->vertices[i], dir);
        if (d > best_dot) {
            best_dot = d;
            best_idx = i;
        }
    }

    return hull->vertices[best_idx];
}

void phys_convex_hull_recompute_bounds(phys_convex_hull_t *hull) {
    if (!hull || hull->vertex_count == 0) {
        return;
    }

    phys_vec3_t cmin = hull->vertices[0];
    phys_vec3_t cmax = hull->vertices[0];
    phys_vec3_t csum = hull->vertices[0];

    for (uint32_t i = 1; i < hull->vertex_count; i++) {
        phys_vec3_t v = hull->vertices[i];
        if (v.x < cmin.x) cmin.x = v.x;
        if (v.y < cmin.y) cmin.y = v.y;
        if (v.z < cmin.z) cmin.z = v.z;
        if (v.x > cmax.x) cmax.x = v.x;
        if (v.y > cmax.y) cmax.y = v.y;
        if (v.z > cmax.z) cmax.z = v.z;
        csum.x += v.x;
        csum.y += v.y;
        csum.z += v.z;
    }

    hull->aabb.min = cmin;
    hull->aabb.max = cmax;
    float inv_n = 1.0f / (float)hull->vertex_count;
    hull->centroid = (phys_vec3_t){csum.x * inv_n, csum.y * inv_n, csum.z * inv_n};
}

phys_aabb_t phys_convex_hull_world_aabb(const phys_convex_hull_t *hull,
                                        phys_vec3_t position,
                                        phys_quat_t rotation) {
    phys_aabb_t result;
    if (!hull || hull->vertex_count == 0) {
        result.min = position;
        result.max = position;
        return result;
    }

    /* Transform first vertex to seed min/max. */
    phys_vec3_t v0 = vec3_add(quat_rotate_vec3(rotation, hull->vertices[0]), position);
    result.min = v0;
    result.max = v0;

    for (uint32_t i = 1; i < hull->vertex_count; i++) {
        phys_vec3_t v = vec3_add(quat_rotate_vec3(rotation, hull->vertices[i]), position);
        if (v.x < result.min.x) result.min.x = v.x;
        if (v.y < result.min.y) result.min.y = v.y;
        if (v.z < result.min.z) result.min.z = v.z;
        if (v.x > result.max.x) result.max.x = v.x;
        if (v.y > result.max.y) result.max.y = v.y;
        if (v.z > result.max.z) result.max.z = v.z;
    }

    return result;
}
