/**
 * @file gjk_support.c
 * @brief Shared GJK support function implementations.
 *
 * These support functions are used by both the convex narrowphase
 * and the dynamic-dynamic CCD sweep.  Each maps a world-space
 * direction to the furthest surface point on the given primitive.
 *
 * Non-static functions (4):
 *   1. phys_gjk_support_sphere
 *   2. phys_gjk_support_box
 *   3. phys_gjk_support_capsule
 *   4. phys_gjk_support_hull
 */

#include "ferrum/physics/gjk_support.h"

#include <math.h>

#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/physics/convex_hull.h"

/* ── Sphere ────────────────────────────────────────────────────── */

phys_vec3_t phys_gjk_support_sphere(const void *data, phys_vec3_t dir) {
    const phys_gjk_sphere_data_t *s = data;
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-10f) return s->center;
    float inv = s->radius / len;
    return (phys_vec3_t){
        s->center.x + dir.x * inv,
        s->center.y + dir.y * inv,
        s->center.z + dir.z * inv,
    };
}

/* ── Box ───────────────────────────────────────────────────────── */

phys_vec3_t phys_gjk_support_box(const void *data, phys_vec3_t dir) {
    const phys_gjk_box_data_t *b = data;
    /* Transform direction into box local space. */
    phys_vec3_t local_dir = quat_inv_rotate_vec3(b->rotation, dir);
    /* Support in local space: sign(dir) * half_extents. */
    phys_vec3_t local_sup = {
        local_dir.x >= 0 ? b->half_extents.x : -b->half_extents.x,
        local_dir.y >= 0 ? b->half_extents.y : -b->half_extents.y,
        local_dir.z >= 0 ? b->half_extents.z : -b->half_extents.z,
    };
    /* Transform back to world space. */
    phys_vec3_t world_sup = quat_rotate_vec3(b->rotation, local_sup);
    return (phys_vec3_t){
        b->center.x + world_sup.x,
        b->center.y + world_sup.y,
        b->center.z + world_sup.z,
    };
}

/* ── Capsule ───────────────────────────────────────────────────── */

phys_vec3_t phys_gjk_support_capsule(const void *data, phys_vec3_t dir) {
    const phys_gjk_capsule_data_t *c = data;
    /* Capsule axis in world space: rotate (0,1,0). */
    phys_vec3_t axis = quat_rotate_vec3(c->rotation, (phys_vec3_t){0, 1, 0});
    /* Pick the endpoint furthest along dir. */
    float d = axis.x * dir.x + axis.y * dir.y + axis.z * dir.z;
    phys_vec3_t endpoint;
    if (d >= 0) {
        endpoint = (phys_vec3_t){
            c->center.x + axis.x * c->half_height,
            c->center.y + axis.y * c->half_height,
            c->center.z + axis.z * c->half_height,
        };
    } else {
        endpoint = (phys_vec3_t){
            c->center.x - axis.x * c->half_height,
            c->center.y - axis.y * c->half_height,
            c->center.z - axis.z * c->half_height,
        };
    }
    /* Add radius along dir. */
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-10f) return endpoint;
    float inv = c->radius / len;
    return (phys_vec3_t){
        endpoint.x + dir.x * inv,
        endpoint.y + dir.y * inv,
        endpoint.z + dir.z * inv,
    };
}

/* ── Convex hull ───────────────────────────────────────────────── */

phys_vec3_t phys_gjk_support_hull(const void *data, phys_vec3_t dir) {
    const phys_gjk_hull_data_t *h = data;
    /* Transform direction into hull local space. */
    phys_vec3_t local_dir = quat_inv_rotate_vec3(h->rotation, dir);
    /* Get support point in local space. */
    phys_vec3_t local_sup = phys_convex_hull_support(h->hull, local_dir);
    /* Transform to world space. */
    phys_vec3_t world_sup = quat_rotate_vec3(h->rotation, local_sup);
    return (phys_vec3_t){
        h->center.x + world_sup.x,
        h->center.y + world_sup.y,
        h->center.z + world_sup.z,
    };
}
