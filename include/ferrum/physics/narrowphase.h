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

struct phys_body;
struct phys_collider;
struct phys_sphere;
struct phys_box;
struct phys_capsule;
struct phys_collision_pair;
struct phys_contact_point;

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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_NARROWPHASE_H */
