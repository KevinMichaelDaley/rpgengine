/**
 * @file mesh_narrowphase.h
 * @brief Primitive-vs-triangle and primitive-vs-mesh narrowphase.
 *
 * Provides contact generation between convex primitives (sphere, box,
 * capsule) and individual triangles, plus BVH-accelerated mesh variants
 * that traverse the mesh BVH to find candidate triangles.
 *
 * ## Conventions
 * - Normal points from the triangle surface toward the primitive.
 * - Penetration is positive for overlap, negative for speculative contacts.
 *
 * ## Ownership
 * All functions are stateless and borrow their inputs.
 *
 * ## Nullability
 * All functions are NULL-safe (return false / 0 on NULL inputs).
 */

#ifndef FERRUM_PHYSICS_MESH_NARROWPHASE_H
#define FERRUM_PHYSICS_MESH_NARROWPHASE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/phys_types.h"

struct phys_contact_point;
struct phys_mesh_bvh;

#ifdef __cplusplus
extern "C" {
#endif

/* ── Primitive vs Single Triangle ──────────────────────────────── */

/**
 * @brief Test sphere vs triangle intersection.
 *
 * Finds the closest point on the triangle to the sphere center,
 * then checks overlap (or speculative proximity).
 *
 * @param center       World-space sphere center.
 * @param radius       Sphere radius.
 * @param tri          Triangle (must not be NULL).
 * @param spec_margin  Max separation for speculative contacts (0 = disabled).
 * @param solid        If true, mesh is a closed solid volume; backface normals
 *                     point outward to push primitives out of the interior.
 * @param contact_out  Output contact point.
 * @return true if contact generated.
 */
bool phys_sphere_vs_triangle(
    phys_vec3_t center, float radius,
    const phys_triangle_t *tri,
    float spec_margin,
    bool solid,
    struct phys_contact_point *contact_out);

/**
 * @brief Test oriented box vs triangle intersection.
 *
 * Uses SAT (Separating Axis Theorem) with the triangle normal,
 * box face normals, and cross-product edge axes.
 *
 * @param box_center      World-space box center.
 * @param box_rotation    Box orientation quaternion.
 * @param box_half_extents Box half-extents in local space.
 * @param tri             Triangle (must not be NULL).
 * @param spec_margin     Max separation for speculative contacts.
 * @param contacts_out    Output array (up to 4 contacts).
 * @param max_contacts    Capacity of output array.
 * @return Number of contacts generated (0 = no intersection).
 */
int phys_box_vs_triangle(
    phys_vec3_t box_center, phys_quat_t box_rotation,
    phys_vec3_t box_half_extents,
    const phys_triangle_t *tri,
    float spec_margin,
    struct phys_contact_point *contacts_out,
    int max_contacts);

/**
 * @brief Test capsule vs triangle intersection.
 *
 * Finds the closest point pair between the capsule segment and
 * the triangle, then checks sphere-like overlap at that point.
 *
 * @param cap_center     World-space capsule center.
 * @param cap_rotation   Capsule orientation (Y-axis aligned).
 * @param cap_radius     Capsule radius.
 * @param cap_half_height Capsule half-height (segment half-length).
 * @param tri            Triangle (must not be NULL).
 * @param spec_margin    Max separation for speculative contacts.
 * @param solid          If true, mesh is a closed solid volume.
 * @param contact_out    Output contact point.
 * @return true if contact generated.
 */
bool phys_capsule_vs_triangle(
    phys_vec3_t cap_center, phys_quat_t cap_rotation,
    float cap_radius, float cap_half_height,
    const phys_triangle_t *tri,
    float spec_margin,
    bool solid,
    struct phys_contact_point *contact_out);

/* ── Primitive vs Mesh (BVH-accelerated) ───────────────────────── */

/**
 * @brief Sphere vs triangle mesh with BVH traversal.
 *
 * @param solid  If true, closed solid volume.
 * @return Number of contacts generated.
 */
int phys_sphere_vs_mesh(
    phys_vec3_t center, float radius,
    const phys_triangle_t *triangles,
    const phys_mesh_bvh_t *bvh,
    float spec_margin,
    bool solid,
    struct phys_contact_point *contacts_out,
    int max_contacts);

/**
 * @brief Box vs triangle mesh with BVH traversal.
 *
 * @return Number of contacts generated.
 */
int phys_box_vs_mesh(
    phys_vec3_t box_center, phys_quat_t box_rotation,
    phys_vec3_t box_half_extents,
    const phys_triangle_t *triangles,
    const phys_mesh_bvh_t *bvh,
    float spec_margin,
    struct phys_contact_point *contacts_out,
    int max_contacts);

/**
 * @brief Capsule vs triangle mesh with BVH traversal.
 *
 * @param solid  If true, closed solid volume.
 * @return Number of contacts generated.
 */
int phys_capsule_vs_mesh(
    phys_vec3_t cap_center, phys_quat_t cap_rotation,
    float cap_radius, float cap_half_height,
    const phys_triangle_t *triangles,
    const phys_mesh_bvh_t *bvh,
    float spec_margin,
    bool solid,
    struct phys_contact_point *contacts_out,
    int max_contacts);

/* ── Triangle vs Triangle ──────────────────────────────────────── */

/**
 * @brief Test triangle vs triangle intersection (SAT-based).
 *
 * Tests 11 potential separating axes (2 face normals + 9 edge cross
 * products). Returns the axis with minimum overlap as the contact normal.
 *
 * @param a             First triangle (must not be NULL).
 * @param b             Second triangle (must not be NULL).
 * @param spec_margin   Max separation for speculative contacts (0 = disabled).
 * @param contact_out   Output contact point (must not be NULL).
 * @return true if contact generated.
 */
bool phys_triangle_vs_triangle(
    const phys_triangle_t *a,
    const phys_triangle_t *b,
    float spec_margin,
    struct phys_contact_point *contact_out);

/* ── Mesh vs Mesh (dual BVH) ─────────────────────────────────── */

/**
 * @brief Mesh vs mesh with dual BVH traversal.
 *
 * For each triangle in mesh A, queries mesh B's BVH for overlapping
 * triangles and tests each pair with phys_triangle_vs_triangle.
 *
 * @param tris_a        Mesh A triangles (must not be NULL).
 * @param bvh_a         Mesh A BVH (must not be NULL).
 * @param tris_b        Mesh B triangles (must not be NULL).
 * @param bvh_b         Mesh B BVH (must not be NULL).
 * @param spec_margin   Max separation for speculative contacts.
 * @param contacts_out  Output array.
 * @param max_contacts  Capacity of output array.
 * @return Number of contacts generated.
 */
int phys_mesh_vs_mesh(
    const phys_triangle_t *tris_a,
    const phys_mesh_bvh_t *bvh_a,
    const phys_triangle_t *tris_b,
    const phys_mesh_bvh_t *bvh_b,
    float spec_margin,
    struct phys_contact_point *contacts_out,
    int max_contacts);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_MESH_NARROWPHASE_H */
