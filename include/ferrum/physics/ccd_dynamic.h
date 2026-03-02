/**
 * @file ccd_dynamic.h
 * @brief Dynamic-vs-dynamic swept CCD with solver manifold output.
 *
 * For each broadphase pair where at least one body has PHYS_BODY_FLAG_CCD
 * and both bodies are dynamic primitives (not mesh/halfspace), interpolates
 * both bodies' positions and orientations from prev→curr, bisects [0,1]
 * for the time of impact (TOI), and runs GJK+EPA at the TOI to produce
 * contact manifolds.  These manifolds are injected into the solver
 * pipeline alongside normal narrowphase manifolds.
 *
 * Public types (1):
 *   1. phys_ccd_dynamic_args_t
 *
 * Public functions (1):
 *   1. phys_stage_ccd_dynamic
 */

#ifndef FERRUM_PHYSICS_CCD_DYNAMIC_H
#define FERRUM_PHYSICS_CCD_DYNAMIC_H

#include <stdint.h>

#include "ferrum/physics/manifold.h"
#include "ferrum/physics/broadphase.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_body;
struct phys_collider;
struct phys_frame_arena;

/**
 * @brief Arguments for the dynamic-dynamic CCD stage.
 *
 * Ownership: caller owns all pointed-to arrays.  The stage reads
 * bodies_prev and bodies_curr, and appends manifolds to manifolds_out.
 */
typedef struct phys_ccd_dynamic_args {
    /** Body states at the beginning of the substep (sweep origin). */
    const struct phys_body *bodies_prev;

    /** Body states after integration (sweep destination). */
    const struct phys_body *bodies_curr;

    /** Per-body collider descriptors. */
    const struct phys_collider *colliders;

    /** Shape pools (indexed by collider.shape_index). */
    const void *spheres;      /**< phys_sphere_t array. */
    const void *capsules;     /**< phys_capsule_t array. */
    const void *boxes;        /**< phys_box_t array. */

    /** Broadphase pairs to check. Only pairs where at least one body
     *  has PHYS_BODY_FLAG_CCD and both are dynamic primitives are
     *  processed; all others are skipped. */
    const phys_collision_pair_t *pairs;
    uint32_t pair_count;

    /** Total body count (for bounds checking). */
    uint32_t body_count;

    /** Output manifold buffer.  CCD manifolds are appended starting at
     *  manifolds_out[*manifold_count_out].  The caller must ensure the
     *  buffer has room up to max_manifolds entries total. */
    phys_manifold_t *manifolds_out;
    uint32_t *manifold_count_out;
    uint32_t max_manifolds;

    /** Frame arena for scratch allocations. */
    struct phys_frame_arena *arena;

    /** Substep timestep (seconds). Used for bounding-radius displacement
     *  check to skip slow pairs. */
    float dt;
} phys_ccd_dynamic_args_t;

/**
 * @brief Run dynamic-dynamic CCD sweep and emit solver manifolds.
 *
 * For each qualifying pair, interpolates both bodies from prev→curr
 * using lerp (position) + slerp (orientation), bisects for the earliest
 * overlap time via GJK, then runs EPA at that time to extract contact
 * normal, depth, and points.  The resulting manifolds are appended to
 * the output buffer.
 *
 * @param args  CCD dynamic arguments.  NULL-safe (no-op).
 * @return Number of CCD manifolds generated.
 *
 * Side effects: writes to manifolds_out, increments *manifold_count_out.
 */
int phys_stage_ccd_dynamic(const phys_ccd_dynamic_args_t *args);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_CCD_DYNAMIC_H */
