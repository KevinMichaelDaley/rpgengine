#include <math.h>

#include "ferrum/math/quat_angle.h"

static float clampf_(float x, float lo, float hi) {
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

float fr_quat_angle_degrees_between(quat_t a, quat_t b) {
    /* Normalize to avoid drift from quantization or integration. */
    a = quat_normalize_safe(a, 1e-12f);
    b = quat_normalize_safe(b, 1e-12f);

    /* Relative rotation qrel = inv(a) * b. For unit quats, inv == conjugate. */
    quat_t qrel = quat_mul(quat_conjugate(a), b);
    qrel = quat_normalize_safe(qrel, 1e-12f);

    /* q and -q represent the same rotation; use |w| to get shortest angle. */
    float w = fabsf(qrel.w);
    w = clampf_(w, -1.0f, 1.0f);

    const float angle_rad = 2.0f * acosf(w);
    const float rad_to_deg = 180.0f / 3.14159265358979323846f;
    return angle_rad * rad_to_deg;
}

quat_t fr_quat_integrate_angular_velocity(quat_t q, vec3_t omega_rad_s, float dt_s, float epsilon) {
    if (dt_s == 0.0f) {
        return q;
    }

    const float wx = omega_rad_s.x;
    const float wy = omega_rad_s.y;
    const float wz = omega_rad_s.z;
    const float speed = sqrtf(wx * wx + wy * wy + wz * wz);
    if (!(speed > epsilon)) {
        return q;
    }

    vec3_t axis = (vec3_t){wx / speed, wy / speed, wz / speed};
    const float radians = speed * dt_s;

    quat_t dq = quat_from_axis_angle(axis, radians, epsilon);
    quat_t out = quat_mul(q, dq);
    return quat_normalize_safe(out, epsilon);
}
