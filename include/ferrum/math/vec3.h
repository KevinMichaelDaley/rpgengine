#ifndef FERRUM_MATH_VEC3_H
#define FERRUM_MATH_VEC3_H

#include <math.h>

/** @file
 * @brief 3D float vector API.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** 3D vector (float). */
typedef struct vec3 {
    float x;
    float y;
    float z;
} vec3_t;

/**
 * @brief Compute dot product of two vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Dot product.
 */
static inline float vec3_dot(vec3_t a, vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/**
 * @brief Add two vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return a + b.
 */
static inline vec3_t vec3_add(vec3_t a, vec3_t b) {
    vec3_t r = {a.x + b.x, a.y + b.y, a.z + b.z};
    return r;
}

/**
 * @brief Subtract vectors.
 * @param a Minuend.
 * @param b Subtrahend.
 * @return a - b.
 */
static inline vec3_t vec3_sub(vec3_t a, vec3_t b) {
    vec3_t r = {a.x - b.x, a.y - b.y, a.z - b.z};
    return r;
}

/**
 * @brief Scale a vector by a scalar.
 * @param v Vector to scale.
 * @param s Scalar multiplier.
 * @return Scaled vector.
 */
static inline vec3_t vec3_scale(vec3_t v, float s) {
    vec3_t r = {v.x * s, v.y * s, v.z * s};
    return r;
}

/**
 * @brief Compute squared magnitude (length squared).
 * @param v Vector.
 * @return Vector length squared.
 */
static inline float vec3_magnitude_sq(vec3_t v) {
    return vec3_dot(v, v);
}

/**
 * @brief Compute magnitude (length).
 * @param v Vector.
 * @return Vector length.
 */
static inline float vec3_magnitude(vec3_t v) {
    return sqrtf(vec3_dot(v, v));
}

/**
 * @brief Compute squared distance between two points.
 * @param a First point.
 * @param b Second point.
 * @return Squared distance.
 */
static inline float vec3_distance_sq(vec3_t a, vec3_t b) {
    return vec3_magnitude_sq(vec3_sub(b, a));
}

/**
 * @brief Compute distance between two points.
 * @param a First point.
 * @param b Second point.
 * @return Distance.
 */
static inline float vec3_distance(vec3_t a, vec3_t b) {
    return vec3_magnitude(vec3_sub(b, a));
}

/**
 * @brief Normalize vector with epsilon guard.
 * @param v Vector to normalize.
 * @param epsilon Minimum length; if length <= epsilon, returns zero vector.
 * @return Normalized vector or zero.
 */
vec3_t vec3_normalize_safe(vec3_t v, float epsilon);

/**
 * @brief Linear interpolation between vectors.
 * @param a Start vector.
 * @param b End vector.
 * @param t Interpolation factor (not clamped).
 * @return Interpolated vector.
 */
static inline vec3_t vec3_lerp(vec3_t a, vec3_t b, float t) {
    vec3_t r = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    return r;
}

/**
 * @brief Cross product.
 * @param a First vector.
 * @param b Second vector.
 * @return a × b.
 */
static inline vec3_t vec3_cross(vec3_t a, vec3_t b) {
    vec3_t r = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return r;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_MATH_VEC3_H */
