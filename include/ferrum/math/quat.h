#ifndef FERRUM_MATH_QUAT_H
#define FERRUM_MATH_QUAT_H

#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"

/** @file
 * @brief Quaternion operations.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Quaternion stored as (x, y, z, w). */
typedef struct quat {
    float x;
    float y;
    float z;
    float w;
} quat_t;

/**
 * @brief Build quaternion from axis-angle.
 * @param axis Rotation axis (need not be unit length).
 * @param radians Rotation angle in radians.
 * @param epsilon Minimum axis length; if too small, returns identity quaternion.
 * @return Normalized quaternion representing rotation.
 */
quat_t quat_from_axis_angle(vec3_t axis, float radians, float epsilon);
/**
 * @brief Normalize quaternion with epsilon guard.
 * @param q Quaternion to normalize.
 * @param epsilon Minimum length; if length <= epsilon, returns identity.
 * @return Normalized quaternion or identity.
 */
quat_t quat_normalize_safe(quat_t q, float epsilon);
/**
 * @brief Conjugate quaternion.
 * @param q Input quaternion.
 * @return Conjugated quaternion.
 */
quat_t quat_conjugate(quat_t q);
/**
 * @brief Multiply quaternions (a * b).
 * @param a Left-hand quaternion.
 * @param b Right-hand quaternion.
 * @return Product quaternion.
 */
quat_t quat_mul(quat_t a, quat_t b);
/**
 * @brief Spherical linear interpolation between quaternions.
 * @param a Start quaternion.
 * @param b End quaternion.
 * @param t Interpolation factor [0,1] (clamped).
 * @param epsilon Minimum length for normalization.
 * @return Interpolated normalized quaternion.
 */
quat_t quat_slerp(quat_t a, quat_t b, float t, float epsilon);
/**
 * @brief Convert quaternion to column-major 4x4 rotation matrix.
 * @param q Quaternion to convert.
 * @param out Output matrix (non-NULL).
 * @return 0 on success, -1 on invalid output pointer.
 */
int quat_to_mat4(quat_t q, mat4_t *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_MATH_QUAT_H */
