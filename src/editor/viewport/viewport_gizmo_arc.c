/**
 * @file viewport_gizmo_arc.c
 * @brief Rotation arc quadrant selection with hysteresis.
 *
 * Each rotation ring renders a 90-degree arc. Since rotation is
 * symmetric, the arc can go in any of 4 quadrants. This module
 * picks the quadrant that best faces the camera, with hysteresis
 * to prevent flickering at decision boundaries.
 *
 * Non-static functions (1 / 4 limit):
 *   gizmo_update_arc_quadrants
 */

#include "ferrum/editor/viewport/viewport_gizmo.h"
#include <math.h>

/** Hysteresis threshold: the dot product must cross this value
 *  (from the "wrong" side) before the quadrant sign flips.
 *  Prevents rapid toggling when the camera is near a boundary. */
#define ARC_HYSTERESIS 0.12f

/**
 * @brief Extract an oriented axis direction from the gizmo orientation matrix.
 */
static vec3_t oriented_axis_(const mat4_t *orient, int col) {
    return (vec3_t){orient->m[col * 4 + 0],
                    orient->m[col * 4 + 1],
                    orient->m[col * 4 + 2]};
}

void gizmo_update_arc_quadrants(gizmo_state_t *gizmo, vec3_t eye_pos) {
    if (!gizmo) return;

    /* View direction from gizmo center toward camera. */
    vec3_t view_dir = vec3_sub(eye_pos, gizmo->position);
    float len = vec3_magnitude(view_dir);
    if (len < 1e-6f) return;
    view_dir = vec3_scale(view_dir, 1.0f / len);

    /* Arc u/v mapping per ring (must match renderer and hit tester):
     *   X ring: u = orient col 1 (+Y), v = orient col 2 (+Z)
     *   Y ring: u = orient col 2 (+Z), v = orient col 0 (+X)
     *   Z ring: u = orient col 0 (+X), v = orient col 1 (+Y) */
    static const int ring_uv[3][2] = {
        {1, 2}, {2, 0}, {0, 1},
    };

    const mat4_t *orient = &gizmo->orientation;

    for (int i = 0; i < 3; i++) {
        vec3_t u_axis = oriented_axis_(orient, ring_uv[i][0]);
        vec3_t v_axis = oriented_axis_(orient, ring_uv[i][1]);

        float dot_u = vec3_dot(view_dir, u_axis);
        float dot_v = vec3_dot(view_dir, v_axis);

        /* Initialize signs if they're zero (first frame). */
        if (gizmo->arc_sign_u[i] == 0) {
            gizmo->arc_sign_u[i] = (dot_u >= 0.0f) ? 1 : -1;
        }
        if (gizmo->arc_sign_v[i] == 0) {
            gizmo->arc_sign_v[i] = (dot_v >= 0.0f) ? 1 : -1;
        }

        /* Only flip if the dot product has crossed past the hysteresis
         * threshold on the opposite side of the current sign. */
        if (gizmo->arc_sign_u[i] > 0 && dot_u < -ARC_HYSTERESIS) {
            gizmo->arc_sign_u[i] = -1;
        } else if (gizmo->arc_sign_u[i] < 0 && dot_u > ARC_HYSTERESIS) {
            gizmo->arc_sign_u[i] = 1;
        }

        if (gizmo->arc_sign_v[i] > 0 && dot_v < -ARC_HYSTERESIS) {
            gizmo->arc_sign_v[i] = -1;
        } else if (gizmo->arc_sign_v[i] < 0 && dot_v > ARC_HYSTERESIS) {
            gizmo->arc_sign_v[i] = 1;
        }
    }
}
