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
static inline quat_t quat_normalize_safe(quat_t q, float epsilon) {
    float len_sq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (len_sq <= epsilon || len_sq == 0.0f) {
        quat_t ident = {0.0f, 0.0f, 0.0f, 1.0f};
        return ident;
    }
    float inv = 1.0f / sqrtf(len_sq);
    quat_t r = {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
    return r;
}

/**
 * @brief Conjugate quaternion.
 * @param q Input quaternion.
 * @return Conjugated quaternion.
 */
static inline quat_t quat_conjugate(quat_t q) {
    quat_t r = {-q.x, -q.y, -q.z, q.w};
    return r;
}

/**
 * @brief Multiply quaternions (a * b).
 * @param a Left-hand quaternion.
 * @param b Right-hand quaternion.
 * @return Product quaternion.
 */
static inline quat_t quat_mul(quat_t a, quat_t b) {
    quat_t r = {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    return r;
}

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

/**
 * @brief Rotate a vector by a quaternion: q * v * q^-1.
 * @param q Unit quaternion representing the rotation.
 * @param v Vector to rotate.
 * @return Rotated vector.
 */
static inline vec3_t quat_rotate_vec3(quat_t q, vec3_t v) {
    vec3_t u = {q.x, q.y, q.z};
    float s = q.w;
    vec3_t t = vec3_scale(vec3_cross(u, v), 2.0f);
    return vec3_add(v, vec3_add(vec3_scale(t, s), vec3_cross(u, t)));
}

/**
 * @brief Rotate a vector by the inverse (conjugate) of a quaternion: q^-1 * v * q.
 * @param q Unit quaternion whose inverse rotation is applied.
 * @param v Vector to rotate.
 * @return Rotated vector.
 */
static inline vec3_t quat_inv_rotate_vec3(quat_t q, vec3_t v) {
    quat_t conj = quat_conjugate(q);
    return quat_rotate_vec3(conj, v);
}

/**
 * @brief Integrate a quaternion by an angular velocity over dt.
 *
 * Computes q_new = exp(0.5 * omega * dt) * q_old.
 *
 * @param q     Current orientation.
 * @param omega Angular velocity vector (radians/sec).
 * @param dt    Timestep (seconds).
 * @return      Updated normalized quaternion.
 */
static inline quat_t quat_integrate_expmap(quat_t q, vec3_t omega, float dt) {
    float wx = omega.x * dt;
    float wy = omega.y * dt;
    float wz = omega.z * dt;
    float theta = sqrtf(wx * wx + wy * wy + wz * wz);

    quat_t dq;
    if (theta > 1e-8f) {
        float half_theta = 0.5f * theta;
        float s = sinf(half_theta) / theta;
        dq.w = cosf(half_theta);
        dq.x = s * wx;
        dq.y = s * wy;
        dq.z = s * wz;
    } else {
        dq.w = 1.0f;
        dq.x = 0.5f * wx;
        dq.y = 0.5f * wy;
        dq.z = 0.5f * wz;
    }

    quat_t result = quat_normalize_safe(quat_mul(dq, q), 1e-12f);

    /* Shortest-path hemisphere consistency. */
    float dot = q.x * result.x + q.y * result.y + q.z * result.z + q.w * result.w;
    if (dot < 0.0f) {
        result.x = -result.x; result.y = -result.y;
        result.z = -result.z; result.w = -result.w;
    }
    return result;
}

/**
 * @brief Build quaternion from Euler angles (XYZ intrinsic order).
 * @param x Rotation around X axis in radians.
 * @param y Rotation around Y axis in radians.
 * @param z Rotation around Z axis in radians.
 * @return Normalized quaternion representing the combined rotation.
 */
quat_t quat_from_euler(float x, float y, float z);

/**
 * @brief Extract rotation quaternion from a 4×4 column-major matrix.
 * @param m  Input matrix (non-NULL).
 * @return   Normalized quaternion representing the rotation.
 */
quat_t quat_from_mat4(const mat4_t *m);

/**
 * @brief Build quaternion from Euler angles in YXZ matrix order.
 * @param x Rotation around X axis in radians.
 * @param y Rotation around Y axis in radians.
 * @param z Rotation around Z axis in radians.
 * @return Normalized quaternion representing the combined rotation.
 */
quat_t quat_from_euler_yxz(float x, float y, float z);

/**
 * @brief Extract Euler angles (YXZ order) from a quaternion.
 * @param q   Unit quaternion to decompose.
 * @param x   Output X rotation in radians.
 * @param y   Output Y rotation in radians.
 * @param z   Output Z rotation in radians.
 */
void quat_to_euler_yxz(quat_t q, float *x, float *y, float *z);

#ifdef __plusplus
} /* extern "C" */
#endif

#endif /* FERRUM_MATH_QUAT_H */
