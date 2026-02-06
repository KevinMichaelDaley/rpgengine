#include "ferrum/physics/body.h"

#include "ferrum/math/constants.h"

void phys_body_set_capsule_inertia(phys_body_t *body, float mass, float radius, float half_height) {
    if (!body) {
        return;
    }

    if (!(mass > 0.0f) || !(radius > 0.0f) || !(half_height > 0.0f)) {
        body->inv_inertia_diag = (phys_vec3_t){0};
        return;
    }

    // Model capsule as cylinder (length L=2*half_height) + 2 hemispheres.
    const float L = 2.0f * half_height;
    const float r2 = radius * radius;
    const float r3 = r2 * radius;

    const float Vc = FERRUM_PI * r2 * L;
    const float Vs = (4.0f / 3.0f) * FERRUM_PI * r3; // full sphere volume (2 hemis)
    const float Vtot = Vc + Vs;

    const float mc = mass * (Vc / Vtot);
    const float ms = mass * (Vs / Vtot);
    const float mh = 0.5f * ms;

    // Cylinder inertia about its COM, aligned with +Y.
    const float Iyy_c = 0.5f * mc * r2;
    const float Ixx_c = (1.0f / 12.0f) * mc * (3.0f * r2 + L * L);

    // Hemisphere inertia about its own COM.
    // For a solid hemisphere cut through the sphere center:
    // - I about symmetry axis through COM is same as through sphere center: 2/5 m r^2
    // - I about transverse axes through COM: I_center - m*(3r/8)^2
    const float d = (3.0f / 8.0f) * radius;
    const float Iyy_h_com = (2.0f / 5.0f) * mh * r2;
    const float Ixx_h_center = (2.0f / 5.0f) * mh * r2;
    const float Ixx_h_com = Ixx_h_center - mh * d * d;

    // Hemisphere COM offset from capsule origin along +Y.
    const float y_off = half_height + d;
    const float Ixx_h_origin = Ixx_h_com + mh * y_off * y_off;

    const float Ixx = Ixx_c + 2.0f * Ixx_h_origin;
    const float Iyy = Iyy_c + 2.0f * Iyy_h_com;
    const float Izz = Ixx;

    body->inv_inertia_diag = (phys_vec3_t){
        (Ixx > 0.0f) ? (1.0f / Ixx) : 0.0f,
        (Iyy > 0.0f) ? (1.0f / Iyy) : 0.0f,
        (Izz > 0.0f) ? (1.0f / Izz) : 0.0f,
    };
}
