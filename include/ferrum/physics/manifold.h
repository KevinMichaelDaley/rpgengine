#ifndef FERRUM_PHYSICS_MANIFOLD_H
#define FERRUM_PHYSICS_MANIFOLD_H

/** @file
 * @brief Contact point and manifold structures for collision detection.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A single contact point between two bodies.
 *
 * Stores world-space and local-space positions, the contact normal
 * (pointing from body A to body B), penetration depth, and a feature
 * ID for persistent contact tracking across frames.
 */
typedef struct phys_contact_point {
    phys_vec3_t point_world;    /**< Contact point in world space. */
    phys_vec3_t local_a;        /**< Contact point in body A local space. */
    phys_vec3_t local_b;        /**< Contact point in body B local space. */
    phys_vec3_t normal;         /**< Contact normal, points from A to B. */
    float penetration;          /**< Positive means overlap. */
    uint32_t feature_id;        /**< For persistent contact tracking. */
} phys_contact_point_t;

/** Maximum number of contact points per manifold. */
#define PHYS_MAX_MANIFOLD_POINTS 4

/**
 * @brief A contact manifold holding up to 4 contact points between a body pair.
 *
 * Stores material properties and warmstarting impulses for solver convergence.
 */
typedef struct phys_manifold {
    uint32_t body_a;            /**< Index of body A. */
    uint32_t body_b;            /**< Index of body B. */
    uint8_t point_count;        /**< Number of active contact points (0–4). */
    uint8_t pad[3];             /**< Padding for alignment. */
    phys_contact_point_t points[PHYS_MAX_MANIFOLD_POINTS]; /**< Contact points. */

    float friction;             /**< Combined friction coefficient. */
    float restitution;          /**< Combined restitution coefficient. */

    /* Warmstarting data (from previous frame). */
    float normal_impulse[PHYS_MAX_MANIFOLD_POINTS];       /**< Per-point normal impulse. */
    float tangent_impulse[PHYS_MAX_MANIFOLD_POINTS][2];   /**< Per-point tangent impulses. */
} phys_manifold_t;

/**
 * @brief Initialize a manifold for a body pair.
 * @param m Manifold to initialize. If NULL, no-op.
 * @param body_a Index of body A.
 * @param body_b Index of body B.
 */
void phys_manifold_init(phys_manifold_t *m, uint32_t body_a, uint32_t body_b);

/**
 * @brief Add a contact point to the manifold.
 *
 * If the manifold already has PHYS_MAX_MANIFOLD_POINTS, triggers
 * point reduction to keep the best 4 points.
 *
 * @param m Manifold. If NULL, no-op.
 * @param point Contact point to add. If NULL, no-op.
 */
void phys_manifold_add_point(phys_manifold_t *m, const phys_contact_point_t *point);

/**
 * @brief Reduce the manifold to at most 4 points, keeping the best spread.
 * @param m Manifold. If NULL, no-op.
 */
void phys_manifold_reduce_points(phys_manifold_t *m);

/**
 * @brief Clear all contact points from the manifold.
 * @param m Manifold. If NULL, no-op.
 */
void phys_manifold_clear(phys_manifold_t *m);

/**
 * @brief Combine friction coefficients using geometric mean.
 * @return sqrt(f1 * f2)
 */
float phys_combine_friction(float f1, float f2);

/**
 * @brief Combine restitution coefficients using minimum.
 * @return min(r1, r2)
 */
float phys_combine_restitution(float r1, float r2);

/**
 * @brief Compute feature ID for an edge contact.
 * @return (face << 8) | edge
 */
uint32_t phys_feature_id_edge(uint8_t face, uint8_t edge);

/**
 * @brief Compute feature ID for a face contact.
 * @return 0x10000 | face
 */
uint32_t phys_feature_id_face(uint8_t face);

/**
 * @brief Compute feature ID for a vertex contact.
 * @return 0x20000 | vertex
 */
uint32_t phys_feature_id_vertex(uint8_t vertex);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_MANIFOLD_H */
