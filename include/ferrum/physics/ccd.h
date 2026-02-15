/**
 * @file ccd.h
 * @brief Continuous collision detection for fast-moving bodies vs static mesh.
 *
 * Provides a swept-sphere / swept-capsule test against triangle meshes.
 * After the integrator moves bodies, the CCD stage checks whether any
 * body with PHYS_BODY_FLAG_CCD has swept through static geometry during
 * the substep.  If a time-of-impact (TOI) is found, the body's position
 * is clamped to the impact point and its velocity is projected.
 *
 * ## Conventions
 * - Only static-dynamic pairs are tested.
 * - CCD is opt-in per body via PHYS_BODY_FLAG_CCD.
 * - Only sphere and capsule shapes are supported.
 *
 * ## Ownership
 * All functions are stateless and borrow their inputs.
 */

#ifndef FERRUM_PHYSICS_CCD_H
#define FERRUM_PHYSICS_CCD_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_types.h"
#include "ferrum/physics/mesh_collider.h"

struct phys_body;
struct phys_collider;
struct phys_constraint;
struct phys_frame_arena;
struct phys_mesh_shape;

#ifdef __cplusplus
extern "C" {
#endif

/* ── Ray vs Triangle ───────────────────────────────────────────── */

/**
 * @brief Ray-triangle intersection (Möller–Trumbore).
 *
 * @param ray_origin  Ray start point.
 * @param ray_dir     Ray direction (need not be unit length).
 * @param tri         Triangle to test.
 * @param t_out       Output: parametric distance along ray (0 = origin).
 * @return true if the ray hits the triangle with t in [0, 1].
 */
bool phys_ray_vs_triangle(
    phys_vec3_t ray_origin,
    phys_vec3_t ray_dir,
    const phys_triangle_t *tri,
    float *t_out);

/* ── Swept Sphere vs Triangle ──────────────────────────────────── */

/**
 * @brief Swept sphere vs single triangle — find earliest TOI.
 *
 * Tests whether a sphere moving from @p start to @p end intersects
 * the triangle.  Returns the earliest parametric time t in [0, 1].
 *
 * @param start       Sphere center at t=0.
 * @param end         Sphere center at t=1.
 * @param radius      Sphere radius.
 * @param tri         Triangle.
 * @param t_out       Output: earliest time of impact in [0, 1].
 * @param normal_out  Output: contact normal at impact (toward sphere).
 * @return true if intersection found.
 */
bool phys_swept_sphere_vs_triangle(
    phys_vec3_t start,
    phys_vec3_t end,
    float radius,
    const phys_triangle_t *tri,
    float *t_out,
    phys_vec3_t *normal_out);

/* ── Swept Sphere vs Mesh ──────────────────────────────────────── */

/**
 * @brief Swept sphere vs mesh BVH — find earliest TOI.
 *
 * Builds a swept AABB from start to end (inflated by radius),
 * queries the BVH for candidate triangles, and tests each.
 *
 * @param start       Sphere center at t=0.
 * @param end         Sphere center at t=1.
 * @param radius      Sphere radius.
 * @param triangles   Triangle array.
 * @param bvh         Pre-built BVH.
 * @param t_out       Output: earliest TOI in [0, 1].
 * @param normal_out  Output: contact normal at impact.
 * @param hit_pos_out Output: sphere center at impact.
 * @return true if any intersection found.
 */
bool phys_swept_sphere_vs_mesh(
    phys_vec3_t start,
    phys_vec3_t end,
    float radius,
    const phys_triangle_t *triangles,
    const phys_mesh_bvh_t *bvh,
    float *t_out,
    phys_vec3_t *normal_out,
    phys_vec3_t *hit_pos_out);

/* ── CCD Stage ─────────────────────────────────────────────────── */

/**
 * @brief Arguments for the CCD stage.
 */
typedef struct phys_ccd_args {
    struct phys_body *bodies_prev;     /**< CCD snapshot from previous tick (sweep origin). */
    const struct phys_body *bodies_read; /**< Current-frame read buffer (flags, mass). */
    struct phys_body *bodies_curr;     /**< Post-integration write buffer (modified in-place). */
    const struct phys_collider *colliders;
    const struct phys_mesh_shape *meshes;
    const struct phys_constraint *constraints; /**< Constraint array for neighbor propagation. */
    struct phys_frame_arena *arena;    /**< Frame arena for scratch allocations. */
    uint32_t constraint_count;
    uint32_t mesh_count;
    uint32_t body_count;
    float dt;

    /* Shape pools for radius lookup. */
    const void *spheres;    /**< phys_sphere_t array. */
    const void *capsules;   /**< phys_capsule_t array. */
} phys_ccd_args_t;

/**
 * @brief Run CCD pass: clamp fast bodies that swept through static mesh.
 *
 * For each body with PHYS_BODY_FLAG_CCD, if its displacement exceeds
 * its bounding radius, sweep-test against all static mesh shapes.
 * On hit, clamp position to TOI and reflect/zero the velocity component
 * along the contact normal.
 *
 * @param args  CCD arguments.
 * @return Number of bodies clamped.
 */
int phys_stage_ccd(const phys_ccd_args_t *args);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_CCD_H */
