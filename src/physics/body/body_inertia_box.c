#include "ferrum/physics/body.h"

void phys_body_set_box_inertia(phys_body_t *body, float mass, phys_vec3_t half_extents) {
    if (!body) {
        return;
    }

    if (!(mass > 0.0f) || !(half_extents.x > 0.0f) || !(half_extents.y > 0.0f) || !(half_extents.z > 0.0f)) {
        body->inv_inertia_diag = (phys_vec3_t){0};
        return;
    }

    const float wx = 2.0f * half_extents.x;
    const float wy = 2.0f * half_extents.y;
    const float wz = 2.0f * half_extents.z;

    // Solid box about center:
    // Ixx = 1/12 m (wy^2 + wz^2), etc.
    const float Ixx = (1.0f / 12.0f) * mass * (wy * wy + wz * wz);
    const float Iyy = (1.0f / 12.0f) * mass * (wx * wx + wz * wz);
    const float Izz = (1.0f / 12.0f) * mass * (wx * wx + wy * wy);

    body->inv_inertia_diag = (phys_vec3_t){
        (Ixx > 0.0f) ? (1.0f / Ixx) : 0.0f,
        (Iyy > 0.0f) ? (1.0f / Iyy) : 0.0f,
        (Izz > 0.0f) ? (1.0f / Izz) : 0.0f,
    };
}
