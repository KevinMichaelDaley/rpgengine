#ifndef FERRUM_MATH_VEC2_H
#define FERRUM_MATH_VEC2_H

/** @file
 * @brief 2D float vector API.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** 2D vector (float). */
typedef struct vec2 {
    float x;
    float y;
} vec2_t;

/**
 * @brief Compute dot product of two vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Dot product (a · b).
 */
float vec2_dot(vec2_t a, vec2_t b);
/**
 * @brief Add two vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return a + b.
 */
vec2_t vec2_add(vec2_t a, vec2_t b);
/**
 * @brief Subtract vectors.
 * @param a Minuend.
 * @param b Subtrahend.
 * @return a - b.
 */
vec2_t vec2_sub(vec2_t a, vec2_t b);
/**
 * @brief Scale a vector.
 * @param v Vector to scale.
 * @param s Scalar multiplier.
 * @return Scaled vector.
 */
vec2_t vec2_scale(vec2_t v, float s);
/**
 * @brief Compute vector magnitude.
 * @param v Vector.
 * @return Length of v.
 */
float vec2_magnitude(vec2_t v);
/**
 * @brief Normalize vector with epsilon guard.
 * @param v Vector to normalize.
 * @param epsilon Minimum length; if length <= epsilon, returns zero vector.
 * @return Normalized vector or zero.
 */
vec2_t vec2_normalize_safe(vec2_t v, float epsilon);
/**
 * @brief Linear interpolation between vectors.
 * @param a Start vector.
 * @param b End vector.
 * @param t Interpolation factor (not clamped).
 * @return a + (b - a) * t.
 */
vec2_t vec2_lerp(vec2_t a, vec2_t b, float t);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_MATH_VEC2_H */
