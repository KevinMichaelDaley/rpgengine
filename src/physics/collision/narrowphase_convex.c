/**
 * @file narrowphase_convex.c
 * @brief Narrowphase collision tests for convex hull shapes.
 *
 * Each function constructs GJK support function closures for the
 * involved primitives, runs GJK for intersection, and EPA for
 * penetration depth when overlapping.
 *
 * Non-static functions (4):
 *   1. phys_sphere_vs_convex
 *   2. phys_box_vs_convex
 *   3. phys_capsule_vs_convex
 *   4. phys_convex_vs_convex
 */

#include "ferrum/physics/narrowphase_convex.h"

#include <math.h>
#include <string.h>

#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/physics/gjk_epa.h"

/* ── Support function data structures ──────────────────────────── */

/** Sphere support data. */
typedef struct sphere_data {
    phys_vec3_t center;
    float radius;
} sphere_data_t;

/** Box support data (world-space orientation). */
typedef struct box_data {
    phys_vec3_t center;
    phys_quat_t rotation;
    phys_vec3_t half_extents;
} box_data_t;

/** Capsule support data (world-space orientation). */
typedef struct capsule_data {
    phys_vec3_t center;
    phys_quat_t rotation;
    float radius;
    float half_height;
} capsule_data_t;

/** Convex hull support data (world-space transform). */
typedef struct hull_data {
    const phys_convex_hull_t *hull;
    phys_vec3_t center;
    phys_quat_t rotation;
} hull_data_t;

/* ── Support functions ─────────────────────────────────────────── */

static phys_vec3_t support_sphere(const void *data, phys_vec3_t dir) {
    const sphere_data_t *s = data;
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-10f) return s->center;
    float inv = s->radius / len;
    return (phys_vec3_t){
        s->center.x + dir.x * inv,
        s->center.y + dir.y * inv,
        s->center.z + dir.z * inv,
    };
}

static phys_vec3_t support_box(const void *data, phys_vec3_t dir) {
    const box_data_t *b = data;
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

static phys_vec3_t support_capsule(const void *data, phys_vec3_t dir) {
    const capsule_data_t *c = data;
    /* Capsule axis in world space: rotate (0,1,0). */
    phys_vec3_t axis = quat_rotate_vec3(c->rotation, (phys_vec3_t){0, 1, 0});
    /* Pick the endpoint that's furthest along dir. */
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

static phys_vec3_t support_hull(const void *data, phys_vec3_t dir) {
    const hull_data_t *h = data;
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

/* ── Contact construction helper ───────────────────────────────── */

/** Fill contact_out from GJK/EPA result. Returns true if valid contact. */
static bool fill_contact(const phys_gjk_result_t *r,
                          float speculative_margin,
                          phys_vec3_t center_a, phys_vec3_t center_b,
                          struct phys_contact_point *contact_out) {
    if (!r->intersecting) {
        /* Separated — check speculative margin. */
        if (speculative_margin <= 0 || r->distance > speculative_margin) {
            return false;
        }
        /* Speculative contact. */
        contact_out->point_world = (phys_vec3_t){
            (r->closest_a.x + r->closest_b.x) * 0.5f,
            (r->closest_a.y + r->closest_b.y) * 0.5f,
            (r->closest_a.z + r->closest_b.z) * 0.5f,
        };
        phys_vec3_t diff = {
            r->closest_b.x - r->closest_a.x,
            r->closest_b.y - r->closest_a.y,
            r->closest_b.z - r->closest_a.z,
        };
        float len = sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
        if (len > 1e-8f) {
            contact_out->normal = (phys_vec3_t){diff.x / len, diff.y / len, diff.z / len};
        } else {
            contact_out->normal = (phys_vec3_t){0, 1, 0};
        }
        contact_out->penetration = -r->distance;  /* Negative for speculative. */
        contact_out->local_a = vec3_sub(contact_out->point_world, center_a);
        contact_out->local_b = vec3_sub(contact_out->point_world, center_b);
        contact_out->feature_id = 0;
        return true;
    }

    /* Overlapping — use EPA result. */
    contact_out->normal = r->normal;
    contact_out->penetration = r->penetration;
    contact_out->point_world = (phys_vec3_t){
        (r->closest_a.x + r->closest_b.x) * 0.5f,
        (r->closest_a.y + r->closest_b.y) * 0.5f,
        (r->closest_a.z + r->closest_b.z) * 0.5f,
    };
    contact_out->local_a = vec3_sub(contact_out->point_world, center_a);
    contact_out->local_b = vec3_sub(contact_out->point_world, center_b);
    contact_out->feature_id = 0;
    return true;
}

/* ── Public API ────────────────────────────────────────────────── */

bool phys_sphere_vs_convex(
    phys_vec3_t sphere_center, float sphere_radius,
    phys_vec3_t hull_center, phys_quat_t hull_rotation,
    const phys_convex_hull_t *hull,
    float speculative_margin,
    struct phys_contact_point *contact_out) {

    if (!hull || !contact_out) return false;

    sphere_data_t sd = {sphere_center, sphere_radius};
    hull_data_t hd = {hull, hull_center, hull_rotation};

    phys_gjk_result_t result;
    bool hit = phys_gjk_intersect(support_sphere, &sd, support_hull, &hd, &result);
    if (hit) {
        phys_epa_penetration(support_sphere, &sd, support_hull, &hd, &result);
    }
    return fill_contact(&result, speculative_margin,
                        sphere_center, hull_center, contact_out);
}

bool phys_box_vs_convex(
    phys_vec3_t box_center, phys_quat_t box_rotation,
    phys_vec3_t box_half_extents,
    phys_vec3_t hull_center, phys_quat_t hull_rotation,
    const phys_convex_hull_t *hull,
    float speculative_margin,
    struct phys_contact_point *contact_out) {

    if (!hull || !contact_out) return false;

    box_data_t bd = {box_center, box_rotation, box_half_extents};
    hull_data_t hd = {hull, hull_center, hull_rotation};

    phys_gjk_result_t result;
    bool hit = phys_gjk_intersect(support_box, &bd, support_hull, &hd, &result);
    if (hit) {
        phys_epa_penetration(support_box, &bd, support_hull, &hd, &result);
    }
    return fill_contact(&result, speculative_margin,
                        box_center, hull_center, contact_out);
}

bool phys_capsule_vs_convex(
    phys_vec3_t capsule_center, phys_quat_t capsule_rotation,
    float capsule_radius, float capsule_half_height,
    phys_vec3_t hull_center, phys_quat_t hull_rotation,
    const phys_convex_hull_t *hull,
    float speculative_margin,
    struct phys_contact_point *contact_out) {

    if (!hull || !contact_out) return false;

    capsule_data_t cd = {capsule_center, capsule_rotation,
                         capsule_radius, capsule_half_height};
    hull_data_t hd = {hull, hull_center, hull_rotation};

    phys_gjk_result_t result;
    bool hit = phys_gjk_intersect(support_capsule, &cd, support_hull, &hd, &result);
    if (hit) {
        phys_epa_penetration(support_capsule, &cd, support_hull, &hd, &result);
    }
    return fill_contact(&result, speculative_margin,
                        capsule_center, hull_center, contact_out);
}

bool phys_convex_vs_convex(
    phys_vec3_t center_a, phys_quat_t rotation_a,
    const phys_convex_hull_t *hull_a,
    phys_vec3_t center_b, phys_quat_t rotation_b,
    const phys_convex_hull_t *hull_b,
    float speculative_margin,
    struct phys_contact_point *contact_out) {

    if (!hull_a || !hull_b || !contact_out) return false;

    hull_data_t ha = {hull_a, center_a, rotation_a};
    hull_data_t hb = {hull_b, center_b, rotation_b};

    phys_gjk_result_t result;
    bool hit = phys_gjk_intersect(support_hull, &ha, support_hull, &hb, &result);
    if (hit) {
        phys_epa_penetration(support_hull, &ha, support_hull, &hb, &result);
    }
    return fill_contact(&result, speculative_margin,
                        center_a, center_b, contact_out);
}
