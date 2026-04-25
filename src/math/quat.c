#include <math.h>

#include "ferrum/math/quat.h"

quat_t quat_from_axis_angle(vec3_t axis, float radians, float epsilon) {
    vec3_t n = vec3_normalize_safe(axis, epsilon);
    float half = radians * 0.5f;
    float s = sinf(half);
    quat_t q = {n.x * s, n.y * s, n.z * s, cosf(half)};
    return quat_normalize_safe(q, epsilon);
}

quat_t quat_slerp(quat_t a, quat_t b, float t, float epsilon) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    if (dot < 0.0f) {
        b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
        dot = -dot;
    }

    if (dot > 0.9995f) {
        quat_t r = {
            a.x + t * (b.x - a.x),
            a.y + t * (b.y - a.y),
            a.z + t * (b.z - a.z),
            a.w + t * (b.w - a.w)
        };
        return quat_normalize_safe(r, epsilon);
    }

    float theta_0 = acosf(dot);
    float theta = theta_0 * t;
    float sin_theta = sinf(theta);
    float sin_theta_0 = sinf(theta_0);

    float s0 = cosf(theta) - dot * sin_theta / sin_theta_0;
    float s1 = sin_theta / sin_theta_0;

    quat_t r = {
        s0 * a.x + s1 * b.x,
        s0 * a.y + s1 * b.y,
        s0 * a.z + s1 * b.z,
        s0 * a.w + s1 * b.w
    };
    return quat_normalize_safe(r, epsilon);
}
