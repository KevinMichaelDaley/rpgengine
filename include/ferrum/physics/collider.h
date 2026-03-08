#ifndef FERRUM_PHYSICS_COLLIDER_H
#define FERRUM_PHYSICS_COLLIDER_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/phys_types.h"

/** @file
 * @brief Primitive collider types and the collider reference that links
 *        a rigid body to its shape data.
 *
 * Each body has a phys_collider_t that stores the shape type, an index
 * into a shape-specific pool (sphere_pool, box_pool, capsule_pool),
 * and a local transform (offset + rotation) relative to the body origin.
 *
 * Shape data is stored separately so that many colliders can share the
 * same shape definition (e.g. identical crate props).
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ── Shape type enum ────────────────────────────────────────────── */

/** Discriminator for the shape referenced by a collider. */
typedef enum phys_shape_type {
    PHYS_SHAPE_SPHERE  = 0,
    PHYS_SHAPE_BOX     = 1,
    PHYS_SHAPE_CAPSULE = 2,
    PHYS_SHAPE_COMPOUND = 3,  /**< Tree of child colliders (Phase 0.3b). */
    PHYS_SHAPE_CONVEX  = 4,   /**< Future — convex hull. */
    PHYS_SHAPE_MESH    = 5,   /**< Future — triangle mesh (Phase 9). */
    PHYS_SHAPE_HALFSPACE = 6, /**< Infinite plane (normal + distance). */
    PHYS_SHAPE_POINT   = 7,   /**< Zero-volume point collider.  Generates a
                               *   single contact when the body center
                               *   penetrates another shape or halfspace. */
    PHYS_SHAPE_COUNT
} phys_shape_type_t;

/* ── Primitive shape structures ─────────────────────────────────── */

/** Sphere shape data. Stored in a dedicated pool indexed by collider. */
typedef struct phys_sphere {
    float radius;
} phys_sphere_t;

_Static_assert(sizeof(phys_sphere_t) == 4, "phys_sphere_t must be 4 bytes");

/** Axis-aligned box shape data (half-extents). */
typedef struct phys_box {
    phys_vec3_t half_extents;
} phys_box_t;

_Static_assert(sizeof(phys_box_t) == 12, "phys_box_t must be 12 bytes");

/** Capsule shape data (cylinder + hemisphere caps along +Y). */
typedef struct phys_capsule {
    float radius;
    float half_height;  /**< Half the cylinder segment length. */
} phys_capsule_t;

_Static_assert(sizeof(phys_capsule_t) == 8, "phys_capsule_t must be 8 bytes");

/**
 * @brief Mesh shape data.
 *
 * Stores a BVH and triangle array for static mesh collision.
 * The BVH and triangle array are caller-owned; the mesh shape
 * merely stores the pointers for the narrowphase to use.
 */
typedef struct phys_mesh_shape {
    const struct phys_triangle *triangles;   /**< Borrowed triangle array. */
    uint32_t tri_count;                      /**< Number of triangles. */
    bool solid;                              /**< Treat as solid volume (closed mesh). */
    struct phys_mesh_bvh bvh;                /**< Pre-built BVH over triangles. */
} phys_mesh_shape_t;

/**
 * @brief Half-space (infinite plane) shape data.
 *
 * Defines a plane by normal and signed distance from origin.
 * The plane equation is: dot(normal, point) = distance.
 * Everything with dot(normal, point) < distance is "behind" the plane
 * (penetrating).  Half-spaces are always solid by definition.
 *
 * The normal must be unit-length.  The distance is the signed offset
 * from the origin along the normal direction.
 */
typedef struct phys_halfspace {
    phys_vec3_t normal;    /**< Outward-facing unit normal. */
    float       distance;  /**< Signed distance from origin. */
} phys_halfspace_t;

_Static_assert(sizeof(phys_halfspace_t) == 16, "phys_halfspace_t must be 16 bytes");

/* ── Collider reference ─────────────────────────────────────────── */

/**
 * @brief Per-body collider reference.
 *
 * Links a body to its shape data via type + pool index, and stores
 * the local transform of the shape relative to the body origin.
 *
 * Ownership: the collider does NOT own the shape data; it merely
 * references it by index into the world's shape-specific pools.
 *
 * The sphere_simplify flag may be set at asset load time for
 * near-spherical shapes when the bounding-sphere ratio
 * (circumradius / inradius) is < 1.3.
 * At T2+ distances, bodies with this flag use a cheap bounding-sphere
 * narrowphase instead of full shape tests.
 */
typedef struct phys_collider {
    phys_shape_type_t type;       /**< Shape discriminator. */
    uint32_t shape_index;         /**< Index into shape-specific pool. */
    phys_vec3_t local_offset;     /**< Offset from body origin. */
    phys_quat_t local_rotation;   /**< Rotation relative to body. */
    uint8_t sphere_simplify;      /**< 1 if eligible for sphere approximation. */
    uint8_t layer_id;             /**< Query layer index (0–31). */
    uint8_t _pad[2];
} phys_collider_t;

_Static_assert(sizeof(phys_collider_t) == 40, "phys_collider_t must be 40 bytes");

/* ── Initializers ───────────────────────────────────────────────── */

/**
 * @brief Initialize a sphere collider reference.
 * @param c      Collider to initialize (non-NULL).
 * @param sphere_idx  Index into the world's sphere shape pool.
 * @param offset Local offset from body origin.
 *
 * Sets local_rotation to identity.  sphere_simplify defaults to 0;
 * set it manually after init if the shape qualifies.
 */
void phys_collider_init_sphere(phys_collider_t *c,
                               uint32_t sphere_idx,
                               phys_vec3_t offset);

/**
 * @brief Initialize a box collider reference.
 * @param c        Collider to initialize (non-NULL).
 * @param box_idx  Index into the world's box shape pool.
 * @param offset   Local offset from body origin.
 * @param rotation Local rotation relative to body.
 */
void phys_collider_init_box(phys_collider_t *c,
                            uint32_t box_idx,
                            phys_vec3_t offset,
                            phys_quat_t rotation);

/**
 * @brief Initialize a capsule collider reference.
 * @param c           Collider to initialize (non-NULL).
 * @param capsule_idx Index into the world's capsule shape pool.
 * @param offset      Local offset from body origin.
 * @param rotation    Local rotation relative to body.
 */
void phys_collider_init_capsule(phys_collider_t *c,
                                uint32_t capsule_idx,
                                phys_vec3_t offset,
                                phys_quat_t rotation);

/**
 * @brief Initialize a mesh collider reference.
 * @param c         Collider to initialize (non-NULL).
 * @param mesh_idx  Index into the world's mesh shape pool.
 * @param offset    Local offset from body origin.
 */
void phys_collider_init_mesh(phys_collider_t *c,
                              uint32_t mesh_idx,
                              phys_vec3_t offset);

/**
 * @brief Initialize a halfspace collider reference.
 * @param c             Collider to initialize (non-NULL).
 * @param halfspace_idx Index into the world's halfspace shape pool.
 * @param offset        Local offset from body origin.
 *
 * Sets local_rotation to identity.  Half-spaces are always solid
 * and have no rotation (the plane orientation is defined by the
 * shape's normal vector).
 */
void phys_collider_init_halfspace(phys_collider_t *c,
                                   uint32_t halfspace_idx,
                                   phys_vec3_t offset);

/**
 * @brief Initialize a convex hull collider reference.
 * @param c          Collider to initialize (non-NULL).
 * @param convex_idx Index into the world's convex_hulls shape pool.
 * @param offset     Local offset from body origin.
 * @param rotation   Local rotation relative to body.
 */
void phys_collider_init_convex(phys_collider_t *c,
                               uint32_t convex_idx,
                               phys_vec3_t offset,
                               phys_quat_t rotation);

/**
 * @brief Initialize a point collider reference.
 *
 * Point colliders have no shape data — the contact is generated at
 * the body's world-space center.
 *
 * @param c        Collider to initialize.
 * @param offset   Local offset from body origin.
 */
void phys_collider_init_point(phys_collider_t *c,
                               phys_vec3_t offset);

/* ── World-space transform helpers ──────────────────────────────── */

/**
 * @brief Compute the world-space center of a collider.
 *
 * Applies body_rot to the collider's local_offset, then adds body_pos.
 *
 * @param c        Collider (if NULL, returns body_pos).
 * @param body_pos Body world position.
 * @param body_rot Body world orientation.
 * @return World-space center of the collider shape.
 */
phys_vec3_t phys_collider_world_center(const phys_collider_t *c,
                                       phys_vec3_t body_pos,
                                       phys_quat_t body_rot);

/**
 * @brief Compute the world-space rotation of a collider.
 *
 * Returns body_rot * c->local_rotation.
 *
 * @param c        Collider (if NULL, returns body_rot).
 * @param body_rot Body world orientation.
 * @return Combined world rotation.
 */
phys_quat_t phys_collider_world_rotation(const phys_collider_t *c,
                                         phys_quat_t body_rot);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_COLLIDER_H */
