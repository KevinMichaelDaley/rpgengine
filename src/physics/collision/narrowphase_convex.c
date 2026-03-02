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
#include "ferrum/physics/gjk_support.h"

/* Use shared GJK support types and functions from gjk_support.h:
 *   phys_gjk_sphere_data_t  / phys_gjk_support_sphere
 *   phys_gjk_box_data_t     / phys_gjk_support_box
 *   phys_gjk_capsule_data_t / phys_gjk_support_capsule
 *   phys_gjk_hull_data_t    / phys_gjk_support_hull
 */

/* Local aliases for brevity. */
typedef phys_gjk_sphere_data_t  sphere_data_t;
typedef phys_gjk_box_data_t     box_data_t;
typedef phys_gjk_capsule_data_t capsule_data_t;
typedef phys_gjk_hull_data_t    hull_data_t;

#define support_sphere phys_gjk_support_sphere
#define support_box    phys_gjk_support_box
#define support_capsule phys_gjk_support_capsule
#define support_hull   phys_gjk_support_hull

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
