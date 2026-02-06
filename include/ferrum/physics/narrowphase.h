#ifndef FERRUM_PHYSICS_NARROWPHASE_H
#define FERRUM_PHYSICS_NARROWPHASE_H

/** @file
 * @brief Narrowphase contact generation stage.
 *
 * Takes broadphase collision pairs and generates contact candidates
 * via shape-specific intersection tests.  Phase 1 supports only
 * sphere-sphere; box/capsule combos are added in Phase 2.
 *
 * All public functions are NULL-safe.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_types.h"
#include "ferrum/physics/manifold.h"

struct phys_body;
struct phys_collider;
struct phys_sphere;
struct phys_box;
struct phys_capsule;
struct phys_collision_pair;

#ifdef __cplusplus
extern "C" {
#endif

/* ── Contact candidate ─────────────────────────────────────────── */

/**
 * @brief A contact candidate produced by narrowphase.
 *
 * Holds the body pair and up to PHYS_MAX_MANIFOLD_POINTS (4) contact
 * points.  Ownership: caller allocates the output buffer.
 */
typedef struct phys_contact_candidate {
    uint32_t body_a;                    /**< Index of body A. */
    uint32_t body_b;                    /**< Index of body B. */
    struct phys_contact_point contacts[4]; /**< PHYS_MAX_MANIFOLD_POINTS */
    uint8_t contact_count;              /**< Number of valid contacts. */
} phys_contact_candidate_t;

/* ── Narrowphase arguments ─────────────────────────────────────── */

/**
 * @brief Input/output arguments for the narrowphase stage.
 *
 * Ownership: the caller owns all pointed-to arrays.  The stage
 * reads bodies, colliders, shape pools, and pairs; it writes to
 * candidates_out and candidate_count_out.
 *
 * Nullability: if args is NULL or any required field is NULL,
 * the stage is a safe no-op.
 */
typedef struct phys_narrowphase_args {
    const struct phys_body *bodies;              /**< Body array (read-only). */
    const struct phys_collider *colliders;        /**< Per-body collider array. */
    const struct phys_sphere *spheres;            /**< Sphere shape pool. */
    const struct phys_box *boxes;                 /**< Box shape pool. */
    const struct phys_capsule *capsules;           /**< Capsule shape pool. */
    const struct phys_collision_pair *pairs;       /**< Broadphase pair array. */
    uint32_t pair_count;                          /**< Number of pairs. */
    phys_contact_candidate_t *candidates_out;     /**< Caller-allocated output. */
    uint32_t *candidate_count_out;                /**< Receives candidate count. */
    uint32_t max_candidates;                      /**< Capacity of output buffer. */
} phys_narrowphase_args_t;

/* ── Public API ─────────────────────────────────────────────────── */

/**
 * @brief Execute the narrowphase stage.
 *
 * For each broadphase pair, performs shape-specific intersection
 * tests and writes contact candidates to the output buffer.
 *
 * @param args  Narrowphase arguments (NULL-safe, no-op if NULL).
 *
 * Side effects: writes to args->candidates_out and
 *               args->candidate_count_out.
 */
void phys_stage_narrowphase(const phys_narrowphase_args_t *args);

/**
 * @brief Test sphere vs sphere intersection.
 *
 * @param center_a  World-space center of sphere A.
 * @param radius_a  Radius of sphere A.
 * @param center_b  World-space center of sphere B.
 * @param radius_b  Radius of sphere B.
 * @param contact_out  Output contact point (non-NULL on true return).
 * @return true if spheres overlap or touch, false otherwise.
 *
 * Normal points from A to B.  Penetration is positive for overlap.
 * If centers coincide, normal defaults to (0,1,0).
 */
bool phys_sphere_vs_sphere(
    phys_vec3_t center_a, float radius_a,
    phys_vec3_t center_b, float radius_b,
    struct phys_contact_point *contact_out);

/**
 * @brief Test sphere vs oriented box intersection.
 *
 * @param sphere_center  World-space center of the sphere.
 * @param sphere_radius  Radius of the sphere.
 * @param box_center     World-space center of the box (OBB).
 * @param box_rotation   World-space orientation of the box.
 * @param box_half_extents  Half-extents of the box in local space.
 * @param contact_out    Output contact point (non-NULL on true return).
 * @return true if sphere and box overlap or touch, false otherwise.
 *
 * Normal points from box to sphere.  Penetration is positive for overlap.
 * If contact_out is NULL, returns false without crashing.
 */
bool phys_sphere_vs_box(
    phys_vec3_t sphere_center, float sphere_radius,
    phys_vec3_t box_center, phys_quat_t box_rotation,
    phys_vec3_t box_half_extents,
    struct phys_contact_point *contact_out);

/**
 * @brief Test sphere vs capsule intersection.
 *
 * The capsule is defined by a center, orientation quaternion, radius,
 * and half-height.  Its line segment goes from
 * center - axis*half_height to center + axis*half_height, where
 * axis = rotate((0,1,0), capsule_rotation).
 *
 * @param sphere_center      World-space center of the sphere.
 * @param sphere_radius      Radius of the sphere.
 * @param capsule_center     World-space center of the capsule.
 * @param capsule_rotation   World-space orientation of the capsule.
 * @param capsule_radius     Radius of the capsule cylinder/caps.
 * @param capsule_half_height Half the cylinder segment length.
 * @param contact_out        Output contact point (non-NULL on true return).
 * @return true if shapes overlap or touch, false otherwise.
 *
 * Normal points from capsule closest point toward sphere center.
 * Penetration is positive for overlap.
 * If sphere center lies exactly on the capsule segment, normal
 * defaults to (0,1,0).
 *
 * NULL-safe: returns false if contact_out is NULL.
 */
bool phys_sphere_vs_capsule(
    phys_vec3_t sphere_center, float sphere_radius,
    phys_vec3_t capsule_center, phys_quat_t capsule_rotation,
    float capsule_radius, float capsule_half_height,
    struct phys_contact_point *contact_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_NARROWPHASE_H */
