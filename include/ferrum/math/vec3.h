#ifndef FERRUM_MATH_VEC3_H
#define FERRUM_MATH_VEC3_H

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
float vec3_dot(vec3_t a, vec3_t b);
/**
 * @brief Add two vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return a + b.
 */
vec3_t vec3_add(vec3_t a, vec3_t b);
/**
 * @brief Subtract vectors.
 * @param a Minuend.
 * @param b Subtrahend.
 * @return a - b.
 */
vec3_t vec3_sub(vec3_t a, vec3_t b);
/**
 * @brief Scale a vector by a scalar.
 * @param v Vector to scale.
 * @param s Scalar multiplier.
 * @return Scaled vector.
 */
vec3_t vec3_scale(vec3_t v, float s);
/**
 * @brief Compute magnitude (length).
 * @param v Vector.
 * @return Vector length.
 */
float vec3_magnitude(vec3_t v);
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
vec3_t vec3_lerp(vec3_t a, vec3_t b, float t);
/**
 * @brief Cross product.
 * @param a First vector.
 * @param b Second vector.
 * @return a × b.
 */
vec3_t vec3_cross(vec3_t a, vec3_t b);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_MATH_VEC3_H */
