/**
 * @file quat_rotate.c
 * @brief Quaternion-vector rotation operations.
 *
 * Uses the optimized formula: v' = v + 2s(u × v) + 2(u × (u × v))
 * where q = (u, s) = (xyz, w).
 *
 * Non-static functions (2):
 *   1. quat_rotate_vec3
 *   2. quat_inv_rotate_vec3
 */

#include "ferrum/math/quat.h"

vec3_t quat_rotate_vec3(quat_t q, vec3_t v) {
    vec3_t u = {q.x, q.y, q.z};
    float s = q.w;
    vec3_t t = vec3_scale(vec3_cross(u, v), 2.0f);
    return vec3_add(v, vec3_add(vec3_scale(t, s), vec3_cross(u, t)));
}

vec3_t quat_inv_rotate_vec3(quat_t q, vec3_t v) {
    quat_t conj = quat_conjugate(q);
    return quat_rotate_vec3(conj, v);
}
