/**
 * @file narrowphase_sphere_tri.c
 * @brief Sphere vs triangle narrowphase intersection test.
 *
 * Uses closest-point-on-triangle to sphere center, then checks
 * overlap (or speculative proximity).
 */

#include <math.h>
#include <stddef.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/mesh_narrowphase.h"

/* ── Closest point on triangle ─────────────────────────────────── */

/**
 * @brief Find the closest point on triangle (a,b,c) to point p.
 *
 * Barycentric projection with edge/vertex voronoi region checks.
 * Returns the closest point in world space.
 */
static phys_vec3_t closest_point_on_triangle(phys_vec3_t p,
                                              phys_vec3_t a,
                                              phys_vec3_t b,
                                              phys_vec3_t c) {
    phys_vec3_t ab = vec3_sub(b, a);
    phys_vec3_t ac = vec3_sub(c, a);
    phys_vec3_t ap = vec3_sub(p, a);

    float d1 = vec3_dot(ab, ap);
    float d2 = vec3_dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a; /* Vertex A region */

    phys_vec3_t bp = vec3_sub(p, b);
    float d3 = vec3_dot(ab, bp);
    float d4 = vec3_dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b; /* Vertex B region */

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return vec3_add(a, vec3_scale(ab, v)); /* Edge AB */
    }

    phys_vec3_t cp_v = vec3_sub(p, c);
    float d5 = vec3_dot(ab, cp_v);
    float d6 = vec3_dot(ac, cp_v);
    if (d6 >= 0.0f && d5 <= d6) return c; /* Vertex C region */

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return vec3_add(a, vec3_scale(ac, w)); /* Edge AC */
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return vec3_add(b, vec3_scale(vec3_sub(c, b), w)); /* Edge BC */
    }

    /* Inside face */
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return vec3_add(a, vec3_add(vec3_scale(ab, v), vec3_scale(ac, w)));
}

/* ── Sphere vs Triangle ────────────────────────────────────────── */

bool phys_sphere_vs_triangle(
    phys_vec3_t center, float radius,
    const phys_triangle_t *tri,
    float spec_margin,
    phys_contact_point_t *contact_out)
{
    if (!tri || !contact_out) return false;

    phys_vec3_t closest = closest_point_on_triangle(
        center, tri->v[0], tri->v[1], tri->v[2]);

    phys_vec3_t diff = vec3_sub(center, closest);
    float dist_sq = vec3_dot(diff, diff);
    float dist = sqrtf(dist_sq);

    /* Compute triangle face normal. */
    phys_vec3_t e0 = vec3_sub(tri->v[1], tri->v[0]);
    phys_vec3_t e1 = vec3_sub(tri->v[2], tri->v[0]);
    phys_vec3_t tri_normal = vec3_cross(e0, e1);
    float tri_len = sqrtf(vec3_dot(tri_normal, tri_normal));
    if (tri_len > 1e-9f) {
        tri_normal = vec3_scale(tri_normal, 1.0f / tri_len);
    } else {
        tri_normal = (phys_vec3_t){0, 1, 0};
    }

    /* Check which side of the triangle the sphere center is on.
     * Negative dot means the center is on the backface side
     * (behind the triangle), indicating the sphere has penetrated
     * through the surface — treat the mesh as solid by pushing the
     * sphere back toward the front face. */
    float side = vec3_dot(vec3_sub(center, tri->v[0]), tri_normal);
    bool backface = (side < 0.0f);

    if (backface) {
        /* Sphere has penetrated through the triangle surface.
         * Push it back toward the front face (flip the normal). */
        contact_out->normal = vec3_scale(tri_normal, -1.0f);
        contact_out->penetration = radius + fabsf(side);
        contact_out->point_world = closest;
        contact_out->feature_id = 0;
        return true;
    }

    /* Front-face: standard closest-point overlap test. */
    float threshold = radius + spec_margin;
    if (dist_sq > threshold * threshold) return false;

    if (dist < 1e-6f) {
        /* Center is on the triangle surface. */
        contact_out->normal = tri_normal;
        contact_out->penetration = radius;
    } else {
        contact_out->normal = vec3_scale(diff, 1.0f / dist);
        contact_out->penetration = radius - dist;
    }

    contact_out->point_world = closest;
    contact_out->feature_id = 0;
    return true;
}
