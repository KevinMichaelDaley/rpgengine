#ifndef FERRUM_MATH_VEC4_H
#define FERRUM_MATH_VEC4_H

/** @file
 * @brief 4D float vector API.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** 4D vector (float). */
typedef struct vec4 {
    float x;
    float y;
    float z;
    float w;
} vec4_t;

/**
 * @brief Compute dot product of two vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Dot product.
 */
float vec4_dot(vec4_t a, vec4_t b);
/**
 * @brief Add two vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return a + b.
 */
vec4_t vec4_add(vec4_t a, vec4_t b);
/**
 * @brief Subtract vectors.
 * @param a Minuend.
 * @param b Subtrahend.
 * @return a - b.
 */
vec4_t vec4_sub(vec4_t a, vec4_t b);
/**
 * @brief Scale a vector by a scalar.
 * @param v Vector to scale.
 * @param s Scalar multiplier.
 * @return Scaled vector.
 */
vec4_t vec4_scale(vec4_t v, float s);
/**
 * @brief Compute magnitude (length).
 * @param v Vector.
 * @return Vector length.
 */
float vec4_magnitude(vec4_t v);
/**
 * @brief Normalize vector with epsilon guard.
 * @param v Vector to normalize.
 * @param epsilon Minimum length; if length <= epsilon, returns zero vector.
 * @return Normalized vector or zero.
 */
vec4_t vec4_normalize_safe(vec4_t v, float epsilon);
/**
 * @brief Linear interpolation between vectors.
 * @param a Start vector.
 * @param b End vector.
 * @param t Interpolation factor (not clamped).
 * @return Interpolated vector.
 */
vec4_t vec4_lerp(vec4_t a, vec4_t b, float t);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_MATH_VEC4_H */
