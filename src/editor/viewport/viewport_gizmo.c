/**
 * @file viewport_gizmo.c
 * @brief Gizmo lifecycle, hit testing, and drag computation.
 *
 * Non-static functions: 4 (gizmo_state_init, gizmo_state_set_mode,
 *                          gizmo_hit_test, gizmo_compute_drag_delta).
 */

#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include <math.h>

/** @brief Gizmo axis arrow length (world units, before scale). */
static const float ARROW_LENGTH = 1.0f;
/** @brief Gizmo axis hit radius for arrows (world units, before scale). */
static const float AXIS_HIT_RADIUS = 0.25f;
/** @brief Ring gate multiplier on projected gizmo radius.
 *  If cursor is within this multiple of the gizmo's screen-space
 *  radius from its center, the click is a gizmo interaction.
 *  The closest ring (by screen distance) always wins. */
static const float RING_GATE_MULTIPLIER = 1.8f;

/**
 * @brief Test if a ray passes near a line segment from p0 to p1.
 *
 * Returns the closest distance between the ray and the segment.
 * Uses the parametric closest-approach method.
 */
static float ray_segment_distance_(const editor_ray_t *ray,
                                    vec3_t p0, vec3_t p1) {
    vec3_t d = vec3_sub(p1, p0);        /* Segment direction. */
    vec3_t w = vec3_sub(ray->origin, p0);
    float a = vec3_dot(ray->direction, ray->direction);
    float b = vec3_dot(ray->direction, d);
    float c = vec3_dot(d, d);
    float dd = vec3_dot(ray->direction, w);
    float e = vec3_dot(d, w);
    float denom = a * c - b * b;

    float sc, tc;
    if (fabsf(denom) < 1e-8f) {
        /* Nearly parallel. */
        sc = 0.0f;
        tc = (b > c) ? dd / b : e / c;
    } else {
        sc = (b * e - c * dd) / denom;
        tc = (a * e - b * dd) / denom;
    }

    /* Clamp to valid range. */
    if (sc < 0.0f) sc = 0.0f;
    if (tc < 0.0f) tc = 0.0f;
    if (tc > 1.0f) tc = 1.0f;

    /* Closest points. */
    vec3_t closest_ray = vec3_add(ray->origin,
                                   vec3_scale(ray->direction, sc));
    vec3_t closest_seg = vec3_add(p0, vec3_scale(d, tc));
    vec3_t diff = vec3_sub(closest_ray, closest_seg);

    return vec3_magnitude(diff);
}

void gizmo_state_init(gizmo_state_t *gizmo) {
    gizmo->mode = GIZMO_MODE_TRANSLATE;
    gizmo->active_axis = GIZMO_AXIS_NONE;
    gizmo->basis = TRANSFORM_BASIS_WORLD;
    gizmo->position = (vec3_t){0.0f, 0.0f, 0.0f};
    gizmo->orientation = mat4_identity();
    gizmo->dragging = false;
}

void gizmo_state_set_mode(gizmo_state_t *gizmo, gizmo_mode_t mode) {
    gizmo->mode = mode;
    gizmo->active_axis = GIZMO_AXIS_NONE;
    gizmo->dragging = false;
}

/** Enlarged hit radius for axis endpoints (clickable from any angle). */
static const float TIP_HIT_RADIUS = 0.25f;

/**
 * @brief Compute the closest distance from a ray to a point in 3D.
 *
 * Projects the point onto the ray and returns the perpendicular distance.
 */
static float ray_point_distance_(const editor_ray_t *ray, vec3_t point) {
    vec3_t w = vec3_sub(point, ray->origin);
    float t = vec3_dot(w, ray->direction);
    if (t < 0.0f) t = 0.0f; /* Point is behind ray origin. */
    vec3_t closest = vec3_add(ray->origin,
                               vec3_scale(ray->direction, t));
    return vec3_magnitude(vec3_sub(point, closest));
}

/** Number of sample points for ring screen-space distance test.
 *  Matches RING_SEGMENTS in the renderer so the hit region aligns
 *  exactly with the visible ring mesh edges. */
#define RING_SAMPLE_COUNT 32

/**
 * @brief Project a world-space point to normalized screen coords [0,1].
 *
 * @param vp     View-projection matrix.
 * @param world  World-space point.
 * @param out_x  Output normalized X [0,1] (left to right).
 * @param out_y  Output normalized Y [0,1] (top to bottom).
 * @return true if the point is in front of the camera, false if behind.
 */
static bool project_to_screen_(const mat4_t *vp, vec3_t world,
                                  float *out_x, float *out_y) {
    vec4_t clip = mat4_mul_vec4(*vp, (vec4_t){world.x, world.y, world.z, 1.0f});
    if (clip.w < 1e-6f) return false; /* Behind camera. */
    float inv_w = 1.0f / clip.w;
    /* NDC [-1,1] → screen [0,1].
     * The FBO is displayed Y-flipped by Clay's ortho projection,
     * so NDC +1 (FBO top) = screen bottom.  Don't flip Y here. */
    *out_x = (clip.x * inv_w + 1.0f) * 0.5f;
    *out_y = (clip.y * inv_w + 1.0f) * 0.5f;
    return true;
}

/**
 * @brief Compute the 2D point-to-segment distance.
 *
 * Returns the shortest distance from point (px, py) to the line segment
 * from (ax, ay) to (bx, by).
 */
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

/**
 * @brief Compute the 2D screen-space distance from a cursor to a ring outline.
 *
 * Projects sample points on the ring to screen space and measures the
 * minimum distance from the cursor to any segment between consecutive
 * projected points. This works uniformly for face-on circles, edge-on
 * line segments, and all intermediate ellipses.
 *
 * Also updates *max_screen_radius with the maximum projected distance
 * from the ring center to any sample point (used for gating).
 *
 * @return Distance from cursor to the ring outline in normalized screen
 *         coords, or 1e30 if projection fails.
 */
static float screen_ring_distance_(const mat4_t *vp,
                                     vec3_t center, vec3_t normal,
                                     float radius,
                                     float cursor_x, float cursor_y,
                                     float *max_screen_radius) {
    vec3_t arbitrary = (fabsf(normal.y) < 0.9f)
        ? (vec3_t){0, 1, 0}
        : (vec3_t){1, 0, 0};
    vec3_t u = vec3_normalize_safe(vec3_cross(normal, arbitrary), 1e-8f);
    vec3_t v = vec3_cross(normal, u);

    /* Project the center (needed for max_screen_radius tracking). */
    float cx_s, cy_s;
    if (!project_to_screen_(vp, center, &cx_s, &cy_s)) return 1e30f;

    /* Project all sample points to screen space. */
    float step = 2.0f * 3.14159265f / (float)RING_SAMPLE_COUNT;
    float sx_pts[RING_SAMPLE_COUNT];
    float sy_pts[RING_SAMPLE_COUNT];
    int valid_count = 0;

    for (int i = 0; i < RING_SAMPLE_COUNT; ++i) {
        float angle = (float)i * step;
        float ca = cosf(angle), sa = sinf(angle);
        vec3_t pt = {
            center.x + (u.x * ca + v.x * sa) * radius,
            center.y + (u.y * ca + v.y * sa) * radius,
            center.z + (u.z * ca + v.z * sa) * radius,
        };

        float sx, sy;
        if (!project_to_screen_(vp, pt, &sx, &sy)) continue;

        /* Track max screen radius for gating. */
        if (max_screen_radius) {
            float rdx = sx - cx_s, rdy = sy - cy_s;
            float r = sqrtf(rdx * rdx + rdy * rdy);
            if (r > *max_screen_radius) *max_screen_radius = r;
        }

        sx_pts[valid_count] = sx;
        sy_pts[valid_count] = sy;
        valid_count++;
    }

    if (valid_count < 3) return 1e30f;

    /* Compute projected aspect ratio to detect face-on vs edge-on.
     * Face-on rings (aspect ≈ 1) have their circle boundary pass near
     * many cursor positions, so they need a distance penalty to avoid
     * dominating edge-on rings in the comparison. */
    float local_min_r = 1e30f, local_max_r = 0.0f;
    for (int i = 0; i < valid_count; ++i) {
        float rdx = sx_pts[i] - cx_s, rdy = sy_pts[i] - cy_s;
        float r = sqrtf(rdx * rdx + rdy * rdy);
        if (r < local_min_r) local_min_r = r;
        if (r > local_max_r) local_max_r = r;
    }
    /* Aspect ratio: 0 = perfectly edge-on, 1 = perfectly face-on. */
    float aspect = (local_max_r > 1e-6f) ? (local_min_r / local_max_r) : 0.0f;
    /* Penalty: face-on rings (aspect→1) get distance scaled up by up to 2x.
     * Edge-on rings (aspect→0) get no penalty. */
    float penalty = 1.0f + aspect;

    /* Find minimum distance from cursor to any segment between
     * consecutive projected sample points on the ring outline. */
    float min_dist = 1e30f;
    for (int i = 0; i < valid_count; ++i) {
        int next = (i + 1) % valid_count;
        float d = point_seg_dist_2d_(cursor_x, cursor_y,
                                       sx_pts[i], sy_pts[i],
                                       sx_pts[next], sy_pts[next]);
        if (d < min_dist) min_dist = d;
    }
    return min_dist * penalty;
}

/**
 * @brief Extract an oriented axis direction from the gizmo orientation matrix.
 *
 * Column 0 = oriented X, column 1 = oriented Y, column 2 = oriented Z.
 */
static vec3_t oriented_axis_(const mat4_t *orient, int col) {
    return (vec3_t){orient->m[col * 4 + 0],
                    orient->m[col * 4 + 1],
                    orient->m[col * 4 + 2]};
}

gizmo_axis_t gizmo_hit_test(const gizmo_state_t *gizmo,
                              const struct editor_ray *ray,
                              float gizmo_scale,
                              const mat4_t *vp,
                              float screen_x, float screen_y) {
    if (!gizmo || !ray) return GIZMO_AXIS_NONE;
    if (gizmo->mode == GIZMO_MODE_NONE) return GIZMO_AXIS_NONE;

    float length = ARROW_LENGTH * gizmo_scale;
    float threshold = AXIS_HIT_RADIUS * gizmo_scale;
    vec3_t pos = gizmo->position;
    const mat4_t *orient = &gizmo->orientation;

    gizmo_axis_t best = GIZMO_AXIS_NONE;
    float best_dist = threshold;

    /* Get oriented axis directions. */
    vec3_t axis_dirs[3] = {
        oriented_axis_(orient, 0),
        oriented_axis_(orient, 1),
        oriented_axis_(orient, 2),
    };
    gizmo_axis_t axis_ids[3] = {
        GIZMO_AXIS_X, GIZMO_AXIS_Y, GIZMO_AXIS_Z
    };

    if (gizmo->mode == GIZMO_MODE_ROTATE && vp) {
        /* Screen-space ring selection (two-pass):
         *
         * Gate: project gizmo center to screen, compute screen-space
         *   radius (max projected distance from center to any ring point
         *   across all 3 rings).  If cursor is within GATE_MULTIPLIER *
         *   screen_radius of the center, it's a gizmo click.  This scales
         *   naturally with zoom.
         *
         * Pick: among the 3 rings, select the one whose projected outline
         *   is closest to the cursor in 2D screen space. */
        float cx_s, cy_s;
        bool center_visible = project_to_screen_(vp, pos, &cx_s, &cy_s);

        if (center_visible) {
            /* Compute screen-space ring distances and max screen radius. */
            float dists[3];
            float min_dist = 1e30f;
            int min_idx = -1;
            float max_screen_radius = 0.0f;

            for (int i = 0; i < 3; i++) {
                dists[i] = screen_ring_distance_(vp, pos, axis_dirs[i], length,
                                                  screen_x, screen_y,
                                                  &max_screen_radius);
                if (dists[i] < min_dist) {
                    min_dist = dists[i];
                    min_idx = i;
                }
            }

            /* Gate: cursor within GATE_MULTIPLIER * screen_radius of center. */
            float cursor_dx = screen_x - cx_s;
            float cursor_dy = screen_y - cy_s;
            float cursor_to_center = sqrtf(cursor_dx * cursor_dx +
                                            cursor_dy * cursor_dy);
            float gate = max_screen_radius * RING_GATE_MULTIPLIER;
            if (gate < 0.05f) gate = 0.05f; /* Floor for tiny gizmos. */

            if (min_idx >= 0 && cursor_to_center < gate) {
                best = axis_ids[min_idx];
            }
        }
    } else {
        /* Translate/Scale: test each axis as a line segment from pos
         * to pos + oriented_axis * length. Also test the endpoint as
         * a sphere so edge-on axes remain clickable. */
        float tip_threshold = TIP_HIT_RADIUS * gizmo_scale;
        for (int i = 0; i < 3; i++) {
            vec3_t end = {
                pos.x + axis_dirs[i].x * length,
                pos.y + axis_dirs[i].y * length,
                pos.z + axis_dirs[i].z * length,
            };
            /* Test the full line segment. */
            float dist = ray_segment_distance_(ray, pos, end);
            if (dist < best_dist) {
                best_dist = dist;
                best = axis_ids[i];
            }
            /* Also test the endpoint sphere (larger radius) so the
             * axis tip is clickable even when viewed edge-on. */
            float tip_dist = ray_point_distance_(ray, end);
            if (tip_dist < tip_threshold && tip_dist < best_dist) {
                best_dist = tip_dist;
                best = axis_ids[i];
            }
        }
    }

    return best;
}

vec3_t gizmo_compute_drag_delta(const gizmo_state_t *gizmo,
                                  vec3_t drag_start, vec3_t drag_current) {
    vec3_t zero = {0.0f, 0.0f, 0.0f};
    if (!gizmo || gizmo->active_axis == GIZMO_AXIS_NONE) return zero;

    vec3_t full_delta = vec3_sub(drag_current, drag_start);

    /* Project onto active axis. */
    vec3_t result = zero;
    switch (gizmo->active_axis) {
    case GIZMO_AXIS_X:
        result.x = full_delta.x;
        break;
    case GIZMO_AXIS_Y:
        result.y = full_delta.y;
        break;
    case GIZMO_AXIS_Z:
        result.z = full_delta.z;
        break;
    default:
        break;
    }

    return result;
}
