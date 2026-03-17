/**
 * @file viewport_gizmo_ring_test.c
 * @brief Rotation ring screen-space distance computation.
 *
 * Exposes per-ring distances so per-object gizmo mode can compare
 * ring distances across multiple gizmos before applying the hit
 * threshold, ensuring the globally closest ring is selected.
 *
 * Non-static functions (1 / 4 limit):
 *   gizmo_ring_screen_distances
 */

#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/vec4.h"
#include "ferrum/math/mat4.h"
#include <math.h>

/** Number of sample points per arc (matches renderer and hit tester). */
#define ARC_SAMPLE_COUNT 12

/** Arc angular span in radians (90 degrees). */
#define ARC_SPAN (3.14159265f * 0.5f)

/** Gizmo axis length before scaling. */
#define ARROW_LENGTH 1.0f

/** @brief Extract oriented axis direction from gizmo orientation. */
static vec3_t oriented_axis_(const mat4_t *orient, int col) {
    return (vec3_t){orient->m[col * 4 + 0],
                    orient->m[col * 4 + 1],
                    orient->m[col * 4 + 2]};
}

/** @brief Project a world-space point to normalized screen coords [0,1]. */
static bool project_to_screen_(const mat4_t *vp, vec3_t world,
                                  float *out_x, float *out_y) {
    vec4_t clip = mat4_mul_vec4(*vp,
        (vec4_t){world.x, world.y, world.z, 1.0f});
    if (clip.w < 1e-6f) return false;
    float inv_w = 1.0f / clip.w;
    *out_x = (clip.x * inv_w + 1.0f) * 0.5f;
    *out_y = (clip.y * inv_w + 1.0f) * 0.5f;
    return true;
}

/** @brief 2D point-to-segment distance. */
static float point_seg_dist_2d_(float px, float py,
                                  float ax, float ay,
                                  float bx, float by) {
    float ex = bx - ax, ey = by - ay;
    float len_sq = ex * ex + ey * ey;
    float t = 0.0f;
    if (len_sq > 1e-12f) {
        t = ((px - ax) * ex + (py - ay) * ey) / len_sq;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    float dx = ax + t * ex - px;
    float dy = ay + t * ey - py;
    return sqrtf(dx * dx + dy * dy);
}

/** @brief Screen-space distance from cursor to a quarter-circle arc. */
static float screen_arc_distance_(const mat4_t *vp,
                                    vec3_t center, vec3_t u, vec3_t v,
                                    float radius,
                                    float cursor_x, float cursor_y) {
    float step = ARC_SPAN / (float)ARC_SAMPLE_COUNT;
    float sx_pts[ARC_SAMPLE_COUNT + 1];
    float sy_pts[ARC_SAMPLE_COUNT + 1];
    int valid_count = 0;

    for (int i = 0; i <= ARC_SAMPLE_COUNT; ++i) {
        float angle = (float)i * step;
        float ca = cosf(angle), sa = sinf(angle);
        vec3_t pt = {
            center.x + (u.x * ca + v.x * sa) * radius,
            center.y + (u.y * ca + v.y * sa) * radius,
            center.z + (u.z * ca + v.z * sa) * radius,
        };
        float sx, sy;
        if (!project_to_screen_(vp, pt, &sx, &sy)) continue;
        sx_pts[valid_count] = sx;
        sy_pts[valid_count] = sy;
        valid_count++;
    }

    if (valid_count < 2) return 1e30f;

    float min_dist = 1e30f;
    for (int i = 0; i < valid_count - 1; ++i) {
        float d = point_seg_dist_2d_(cursor_x, cursor_y,
                                       sx_pts[i], sy_pts[i],
                                       sx_pts[i + 1], sy_pts[i + 1]);
        if (d < min_dist) min_dist = d;
    }
    return min_dist;
}

void gizmo_ring_screen_distances(const gizmo_state_t *gizmo,
                                   float gizmo_scale,
                                   const mat4_t *vp,
                                   float screen_x, float screen_y,
                                   float out_dists[3]) {
    out_dists[0] = 1e30f;
    out_dists[1] = 1e30f;
    out_dists[2] = 1e30f;

    if (!gizmo || !vp) return;

    float length = ARROW_LENGTH * gizmo_scale;
    vec3_t pos = gizmo->position;
    const mat4_t *orient = &gizmo->orientation;

    vec3_t axis_dirs[3] = {
        oriented_axis_(orient, 0),
        oriented_axis_(orient, 1),
        oriented_axis_(orient, 2),
    };

    /* Arc u/v mapping per ring (must match renderer and main hit tester):
     *   X ring: u = +Y, v = +Z
     *   Y ring: u = +Z, v = +X
     *   Z ring: u = +X, v = +Y */
    static const int ring_uv[3][2] = {
        {1, 2}, {2, 0}, {0, 1},
    };

    for (int i = 0; i < 3; i++) {
        vec3_t u = vec3_scale(axis_dirs[ring_uv[i][0]],
                               (float)gizmo->arc_sign_u[i]);
        vec3_t v = vec3_scale(axis_dirs[ring_uv[i][1]],
                               (float)gizmo->arc_sign_v[i]);
        out_dists[i] = screen_arc_distance_(vp, pos, u, v, length,
                                              screen_x, screen_y);
    }
}
