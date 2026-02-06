/**
 * @file manifold_core.c
 * @brief Core manifold operations: init, add_point, clear.
 */

#include "ferrum/physics/manifold.h"

#include <math.h>
#include <string.h>

/* ── Static helpers ─────────────────────────────────────────────── */

/**
 * @brief Compute squared distance between two world-space points.
 */
static float dist_sq(const phys_vec3_t *a, const phys_vec3_t *b) {
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float dz = a->z - b->z;
    return dx * dx + dy * dy + dz * dz;
}

/**
 * @brief Compute the area of the triangle formed by three points
 *        (half the magnitude of the cross product of two edge vectors).
 */
static float triangle_area(const phys_vec3_t *a, const phys_vec3_t *b, const phys_vec3_t *c) {
    float abx = b->x - a->x, aby = b->y - a->y, abz = b->z - a->z;
    float acx = c->x - a->x, acy = c->y - a->y, acz = c->z - a->z;
    /* cross product */
    float cx = aby * acz - abz * acy;
    float cy = abz * acx - abx * acz;
    float cz = abx * acy - aby * acx;
    return 0.5f * sqrtf(cx * cx + cy * cy + cz * cz);
}

/* ── Public API (3 non-static functions) ────────────────────────── */

void phys_manifold_init(phys_manifold_t *m, uint32_t body_a, uint32_t body_b) {
    if (!m) { return; }
    memset(m, 0, sizeof(*m));
    m->body_a = body_a;
    m->body_b = body_b;
}

void phys_manifold_add_point(phys_manifold_t *m, const phys_contact_point_t *point) {
    if (!m || !point) { return; }

    if (m->point_count < PHYS_MAX_MANIFOLD_POINTS) {
        m->points[m->point_count] = *point;
        m->point_count++;
        return;
    }

    /* Already at capacity — need to reduce.
     * Temporarily hold all 5 candidates on the stack. */
    phys_contact_point_t candidates[PHYS_MAX_MANIFOLD_POINTS + 1];
    memcpy(candidates, m->points, sizeof(phys_contact_point_t) * PHYS_MAX_MANIFOLD_POINTS);
    candidates[PHYS_MAX_MANIFOLD_POINTS] = *point;

    /* Find the deepest penetration point (step 1). */
    int deepest = 0;
    for (int i = 1; i <= PHYS_MAX_MANIFOLD_POINTS; i++) {
        if (candidates[i].penetration > candidates[deepest].penetration) {
            deepest = i;
        }
    }

    /* Find the point farthest from the deepest (step 2). */
    int farthest = -1;
    float max_d = -1.0f;
    for (int i = 0; i <= PHYS_MAX_MANIFOLD_POINTS; i++) {
        if (i == deepest) { continue; }
        float d = dist_sq(&candidates[deepest].point_world,
                          &candidates[i].point_world);
        if (d > max_d) {
            max_d = d;
            farthest = i;
        }
    }

    /* Find the point that maximizes triangle area with the first two (step 3). */
    int best_tri = -1;
    float max_area = -1.0f;
    for (int i = 0; i <= PHYS_MAX_MANIFOLD_POINTS; i++) {
        if (i == deepest || i == farthest) { continue; }
        float area = triangle_area(&candidates[deepest].point_world,
                                   &candidates[farthest].point_world,
                                   &candidates[i].point_world);
        if (area > max_area) {
            max_area = area;
            best_tri = i;
        }
    }

    /* Find the point that maximizes quad area with the first three (step 4).
     * Quad area ≈ sum of two triangle areas using the new point. */
    int best_quad = -1;
    float max_quad_area = -1.0f;
    for (int i = 0; i <= PHYS_MAX_MANIFOLD_POINTS; i++) {
        if (i == deepest || i == farthest || i == best_tri) { continue; }
        float a1 = triangle_area(&candidates[deepest].point_world,
                                 &candidates[farthest].point_world,
                                 &candidates[i].point_world);
        float a2 = triangle_area(&candidates[best_tri].point_world,
                                 &candidates[farthest].point_world,
                                 &candidates[i].point_world);
        float total = a1 + a2;
        if (total > max_quad_area) {
            max_quad_area = total;
            best_quad = i;
        }
    }

    /* Write the 4 selected points back. */
    m->points[0] = candidates[deepest];
    m->points[1] = candidates[farthest];
    m->points[2] = candidates[best_tri];
    m->points[3] = candidates[best_quad];
    m->point_count = PHYS_MAX_MANIFOLD_POINTS;
}

void phys_manifold_clear(phys_manifold_t *m) {
    if (!m) { return; }
    m->point_count = 0;
}
