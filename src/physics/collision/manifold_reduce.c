/**
 * @file manifold_reduce.c
 * @brief Manifold point reduction — keeps the best 4 contact points.
 *
 * Algorithm:
 *  1. Keep the point with deepest penetration.
 *  2. Keep the point farthest from the deepest.
 *  3. Keep the point that maximizes triangle area with the first two.
 *  4. Keep the point that maximizes quad area with the first three.
 */

#include "ferrum/physics/manifold.h"

#include <math.h>
#include <string.h>

/**
 * @brief Compute squared distance between two world-space points.
 */
static float reduce_dist_sq(const phys_vec3_t *a, const phys_vec3_t *b) {
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float dz = a->z - b->z;
    return dx * dx + dy * dy + dz * dz;
}

/**
 * @brief Compute the area of the triangle formed by three points.
 */
static float reduce_triangle_area(const phys_vec3_t *a, const phys_vec3_t *b,
                                  const phys_vec3_t *c) {
    float abx = b->x - a->x, aby = b->y - a->y, abz = b->z - a->z;
    float acx = c->x - a->x, acy = c->y - a->y, acz = c->z - a->z;
    float cx = aby * acz - abz * acy;
    float cy = abz * acx - abx * acz;
    float cz = abx * acy - aby * acx;
    return 0.5f * sqrtf(cx * cx + cy * cy + cz * cz);
}

void phys_manifold_reduce_points(phys_manifold_t *m) {
    if (!m || m->point_count <= PHYS_MAX_MANIFOLD_POINTS) { return; }

    phys_contact_point_t candidates[PHYS_MAX_MANIFOLD_POINTS + 1];
    uint8_t count = m->point_count;
    if (count > PHYS_MAX_MANIFOLD_POINTS + 1) {
        count = PHYS_MAX_MANIFOLD_POINTS + 1;
    }
    memcpy(candidates, m->points, sizeof(phys_contact_point_t) * count);

    int last = count - 1;

    /* Step 1: keep deepest penetration. */
    int deepest = 0;
    for (int i = 1; i <= last; i++) {
        if (candidates[i].penetration > candidates[deepest].penetration) {
            deepest = i;
        }
    }

    /* Step 2: keep farthest from deepest. */
    int farthest = -1;
    float max_d = -1.0f;
    for (int i = 0; i <= last; i++) {
        if (i == deepest) { continue; }
        float d = reduce_dist_sq(&candidates[deepest].point_world,
                                 &candidates[i].point_world);
        if (d > max_d) {
            max_d = d;
            farthest = i;
        }
    }

    /* Step 3: maximize triangle area with first two. */
    int best_tri = -1;
    float max_area = -1.0f;
    for (int i = 0; i <= last; i++) {
        if (i == deepest || i == farthest) { continue; }
        float area = reduce_triangle_area(&candidates[deepest].point_world,
                                          &candidates[farthest].point_world,
                                          &candidates[i].point_world);
        if (area > max_area) {
            max_area = area;
            best_tri = i;
        }
    }

    /* Step 4: maximize quad area with first three. */
    int best_quad = -1;
    float max_quad_area = -1.0f;
    for (int i = 0; i <= last; i++) {
        if (i == deepest || i == farthest || i == best_tri) { continue; }
        float a1 = reduce_triangle_area(&candidates[deepest].point_world,
                                        &candidates[farthest].point_world,
                                        &candidates[i].point_world);
        float a2 = reduce_triangle_area(&candidates[best_tri].point_world,
                                        &candidates[farthest].point_world,
                                        &candidates[i].point_world);
        float total = a1 + a2;
        if (total > max_quad_area) {
            max_quad_area = total;
            best_quad = i;
        }
    }

    m->points[0] = candidates[deepest];
    m->points[1] = candidates[farthest];
    m->points[2] = candidates[best_tri];
    m->points[3] = candidates[best_quad];
    m->point_count = PHYS_MAX_MANIFOLD_POINTS;
}
