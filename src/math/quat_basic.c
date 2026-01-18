#include <math.h>

#include "ferrum/math/quat.h"

quat_t quat_from_axis_angle(vec3_t axis, float radians, float epsilon) {
    vec3_t n = vec3_normalize_safe(axis, epsilon);
    float half = radians * 0.5f;
    float s = sinf(half);
    quat_t q = {n.x * s, n.y * s, n.z * s, cosf(half)};
    return quat_normalize_safe(q, epsilon);
}

quat_t quat_normalize_safe(quat_t q, float epsilon) {
    float len_sq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (len_sq <= epsilon || len_sq == 0.0f) {
        quat_t ident = {0.0f, 0.0f, 0.0f, 1.0f};
        return ident;
    }
    float inv = 1.0f / sqrtf(len_sq);
    quat_t r = {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
    return r;
}

quat_t quat_conjugate(quat_t q) {
    quat_t r = {-q.x, -q.y, -q.z, q.w};
    return r;
}

quat_t quat_mul(quat_t a, quat_t b) {
    quat_t r = {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    return r;
}
