#ifndef FERRUM_PHYSICS_PHYS_VEC3_OPS_H
#define FERRUM_PHYSICS_PHYS_VEC3_OPS_H

/**
 * @file
 * @brief Physics vector operations (wrappers around math/vec3.h).
 *
 * Physics code should use these instead of local static helper functions.
 * All operations work on phys_vec3_t (bit-compatible with vec3_t).
 */

#include "ferrum/physics/phys_vec3.h"
#include "ferrum/math/vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute dot product of two physics vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Dot product a · b.
 */
static inline float phys_vec3_dot(phys_vec3_t a, phys_vec3_t b) {
    return vec3_dot(a, b);
}

/**
 * @brief Compute squared magnitude.
 * @param v Vector.
 * @return Length squared.
 */
static inline float phys_vec3_magnitude_sq(phys_vec3_t v) {
    return vec3_dot(v, v);
}

/**
 * @brief Compute magnitude (length).
 * @param v Vector.
 * @return Length of v.
 */
static inline float phys_vec3_magnitude(phys_vec3_t v) {
    return vec3_magnitude(v);
}

/**
 * @brief Add two physics vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return a + b.
 */
static inline phys_vec3_t phys_vec3_add(phys_vec3_t a, phys_vec3_t b) {
    return vec3_add(a, b);
}

/**
 * @brief Subtract vectors: a - b.
 * @param a Minuend.
 * @param b Subtrahend.
 * @return a - b.
 */
static inline phys_vec3_t phys_vec3_sub(phys_vec3_t a, phys_vec3_t b) {
    return vec3_sub(a, b);
}

/**
 * @brief Scale physics vector by scalar.
 * @param v Vector.
 * @param s Scalar.
 * @return v * s.
 */
static inline phys_vec3_t phys_vec3_scale(phys_vec3_t v, float s) {
    return vec3_scale(v, s);
}

/**
 * @brief Negate vector.
 * @param v Vector.
 * @return -v.
 */
static inline phys_vec3_t phys_vec3_neg(phys_vec3_t v) {
    return vec3_scale(v, -1.0f);
}

/**
 * @brief Cross product: a × b.
 * @param a First vector.
 * @param b Second vector.
 * @return a × b.
 */
static inline phys_vec3_t phys_vec3_cross(phys_vec3_t a, phys_vec3_t b) {
    return vec3_cross(a, b);
}

/**
 * @brief Normalize with epsilon guard.
 * @param v Vector to normalize.
 * @param epsilon Minimum length; if <= epsilon, returns zero vector.
 * @return Normalized vector or zero.
 */
static inline phys_vec3_t phys_vec3_normalize_safe(phys_vec3_t v, float epsilon) {
    return vec3_normalize_safe(v, epsilon);
}

/**
 * @brief Compute distance between two points.
 * @param a First point.
 * @param b Second point.
 * @return Distance.
 */
static inline float phys_vec3_distance(phys_vec3_t a, phys_vec3_t b) {
    return vec3_distance(a, b);
}

/**
 * @brief Compute squared distance between two points.
 * @param a First point.
 * @param b Second point.
 * @return Squared distance.
 */
static inline float phys_vec3_distance_sq(phys_vec3_t a, phys_vec3_t b) {
    return vec3_distance_sq(a, b);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PHYS_VEC3_OPS_H */
