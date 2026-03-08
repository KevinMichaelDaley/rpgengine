/**
 * @file ccd_dynamic.h
 * @brief Dynamic-vs-dynamic swept CCD with solver manifold output.
 *
 * For each broadphase pair where at least one body has PHYS_BODY_FLAG_CCD
 * and both bodies are dynamic primitives (not mesh/halfspace), extrapolates
 * both bodies forward from their current poses using linear/angular velocity
 * over dt, bisects [0,1] for the time of impact (TOI), and runs GJK+EPA
 * at the TOI to produce contact manifolds.  These manifolds are injected
 * into the solver pipeline alongside normal narrowphase manifolds.
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
struct phys_joint;
struct phys_frame_arena;
struct phys_pair_set;

/**
 * @brief Arguments for the dynamic-dynamic CCD stage.
 *
 * Ownership: caller owns all pointed-to arrays.  The stage reads
 * bodies (current state + velocities) and appends manifolds to
 * manifolds_out.
 */
typedef struct phys_ccd_dynamic_args {
    /** Current body states (positions, orientations, velocities). */
    const struct phys_body *bodies;

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

    /** Optional per-pair skip flags (pair_count entries).  If non-NULL,
     *  set to 1 for each pair where CCD produced a manifold.  The caller
     *  passes this array to the narrowphase to skip duplicate processing. */
    uint8_t *skip_pair_out;

    /** Active joints array.  Pairs connected by a joint are skipped
     *  (jointed bodies are constrained and unlikely to tunnel; letting
     *  narrowphase handle them avoids solver conflicts). */
    const struct phys_joint *joints;
    uint32_t joint_count;

    /** Optional excluded-pair set. When non-NULL, pairs in the set are
     *  skipped before CCD work, matching the fused collision pipeline. */
    const struct phys_pair_set *exclude_set;

    /** Frame arena for scratch allocations. */
    struct phys_frame_arena *arena;

    /** Substep timestep (seconds).  Used both for velocity extrapolation
     *  and bounding-radius displacement check to skip slow pairs. */
    float dt;
} phys_ccd_dynamic_args_t;

/**
 * @brief Run dynamic-dynamic CCD sweep and emit solver manifolds.
 *
 * For each qualifying pair, extrapolates both bodies forward from current
 * pose using velocity × dt, bisects [0,1] for the earliest overlap time
 * via GJK, then runs EPA at that time to extract contact normal, depth,
 * and points.  The resulting manifolds are appended to the output buffer.
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
