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
#include <stddef.h>

/** @brief Gizmo axis arrow length (world units, before scale). */
static const float ARROW_LENGTH = 1.0f;
/** @brief Gizmo axis hit radius for arrows (world units, before scale). */
static const float AXIS_HIT_RADIUS = 0.25f;
/** @brief Maximum screen-space distance (normalized [0,1] coords) from
 *  cursor to ring outline for a hit. Clicks closer than this threshold
 *  to any ring outline count as a gizmo interaction. */
static const float RING_HIT_THRESHOLD = 0.12f;

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
    /* Penalty: face-on rings (aspect→1) get distance scaled up slightly.
     * Edge-on rings (aspect→0) get no penalty. Kept mild so face-on
     * rings remain selectable without excessive precision. */
    float penalty = 1.0f + 0.15f * aspect;

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

/** Plane square inner edge (fraction of axis length from gizmo center). */
#define PLANE_QUAD_MIN 0.20f
/** Plane square outer edge (fraction of axis length from gizmo center). */
#define PLANE_QUAD_MAX 0.40f

/** Extra padding around the plane square for easier hit detection
 *  (fraction of axis length, added to each edge). */
#define PLANE_HIT_PAD 0.06f

/** Normal offset for plane squares (must match renderer). */
#define PLANE_NORMAL_OFFSET 0.08f

/**
 * @brief Test if a ray hits any of the 3 plane constraint squares.
 *
 * Each plane square is a filled quad located between two axis arrows.
 * For the XY plane square, the quad spans [PLANE_QUAD_MIN..MAX] along
 * both the oriented X and Y axes, with Z=0 (in gizmo space).
 * The entire interior of the square is a hit target, with extra padding
 * around the edges for easier selection.
 *
 * Returns the hit plane axis, or GIZMO_AXIS_NONE if no hit.
 * Populates *out_t with the ray parameter of the hit for priority.
 */
static gizmo_axis_t hit_test_plane_squares_(const editor_ray_t *ray,
                                              vec3_t pos,
                                              const vec3_t axis_dirs[3],
                                              float length,
                                              float *out_t) {
    /* Plane pairs: (col_a, col_b, normal_col, result_axis). */
    static const int pairs[3][3] = {
        {0, 1, 2},  /* XY plane, normal = Z */
        {0, 2, 1},  /* XZ plane, normal = Y */
        {1, 2, 0},  /* YZ plane, normal = X */
    };
    static const gizmo_axis_t plane_axes[3] = {
        GIZMO_AXIS_XY, GIZMO_AXIS_XZ, GIZMO_AXIS_YZ
    };

    float pad = PLANE_HIT_PAD * length;
    float lo = PLANE_QUAD_MIN * length - pad;
    float hi = PLANE_QUAD_MAX * length + pad;
    gizmo_axis_t best = GIZMO_AXIS_NONE;
    float best_t = 1e30f;

    for (int p = 0; p < 3; ++p) {
        vec3_t normal = axis_dirs[pairs[p][2]];
        vec3_t a_dir  = axis_dirs[pairs[p][0]];
        vec3_t b_dir  = axis_dirs[pairs[p][1]];

        /* Offset the plane along its normal to match the rendered position. */
        vec3_t plane_pos = {
            pos.x + normal.x * PLANE_NORMAL_OFFSET * length,
            pos.y + normal.y * PLANE_NORMAL_OFFSET * length,
            pos.z + normal.z * PLANE_NORMAL_OFFSET * length,
        };

        /* Ray-plane intersection: t = dot(normal, plane_pos - ray.origin)
         *                             / dot(normal, ray.direction) */
        float denom = vec3_dot(normal, ray->direction);
        if (fabsf(denom) < 1e-8f) continue; /* Parallel to plane. */

        vec3_t to_pos = vec3_sub(plane_pos, ray->origin);
        float t = vec3_dot(normal, to_pos) / denom;
        if (t < 0.0f) continue; /* Behind camera. */

        /* Intersection point in world space, relative to gizmo center
         * (not the offset plane_pos — we measure along axes from center). */
        vec3_t hit_world = vec3_add(ray->origin,
                                     vec3_scale(ray->direction, t));
        vec3_t local = vec3_sub(hit_world, pos);

        /* Project onto the two plane axes. */
        float u = vec3_dot(local, a_dir);
        float v = vec3_dot(local, b_dir);

        /* Check if within the padded square region (full interior). */
        if (u >= lo && u <= hi && v >= lo && v <= hi) {
            if (t < best_t) {
                best_t = t;
                best = plane_axes[p];
            }
        }
    }

    if (out_t) *out_t = best_t;
    return best;
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
        /* Screen-space ring selection:
         *
         * Measure the 2D screen distance from cursor to each ring outline.
         * If the closest ring is within RING_HIT_THRESHOLD, select it.
         * This works correctly for clicks inside or outside the ring. */
        float cx_s, cy_s;
        bool center_visible = project_to_screen_(vp, pos, &cx_s, &cy_s);

        if (center_visible) {
            float dists[3];
            float min_dist = 1e30f;
            int min_idx = -1;

            for (int i = 0; i < 3; i++) {
                dists[i] = screen_ring_distance_(vp, pos, axis_dirs[i], length,
                                                  screen_x, screen_y,
                                                  NULL);
                if (dists[i] < min_dist) {
                    min_dist = dists[i];
                    min_idx = i;
                }
            }

            /* Select the closest ring if cursor is near its outline. */
            if (min_idx >= 0 && min_dist < RING_HIT_THRESHOLD) {
                best = axis_ids[min_idx];
            }
        }
    } else {
        /* Translate/Scale: first test plane constraint squares, then
         * test each axis line segment. Plane squares take priority. */
        float plane_t;
        gizmo_axis_t plane_hit = hit_test_plane_squares_(
            ray, pos, axis_dirs, length, &plane_t);
        if (plane_hit != GIZMO_AXIS_NONE) {
            return plane_hit;
        }

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
    case GIZMO_AXIS_XY:
        result.x = full_delta.x;
        result.y = full_delta.y;
        break;
    case GIZMO_AXIS_XZ:
        result.x = full_delta.x;
        result.z = full_delta.z;
        break;
    case GIZMO_AXIS_YZ:
        result.y = full_delta.y;
        result.z = full_delta.z;
        break;
    default:
        break;
    }

    return result;
}
