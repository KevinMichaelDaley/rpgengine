#include "ferrum/physics/body.h"

void phys_body_set_sphere_inertia(phys_body_t *body, float mass, float radius) {
    if (!body) {
        return;
    }

    if (!(mass > 0.0f) || !(radius > 0.0f)) {
        body->inv_inertia_diag = (phys_vec3_t){0};
        return;
    }

    // Solid sphere: I = 2/5 * m * r^2.
    const float I = (2.0f / 5.0f) * mass * radius * radius;
    const float inv_I = (I > 0.0f) ? (1.0f / I) : 0.0f;
    body->inv_inertia_diag = (phys_vec3_t){inv_I, inv_I, inv_I};
}
