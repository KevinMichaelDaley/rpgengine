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
/** @brief Gizmo axis hit radius (world units, before scale). */
static const float AXIS_HIT_RADIUS = 0.15f;

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

/** Number of sample points for ring hit test. */
#define RING_SAMPLE_COUNT 24

/**
 * @brief Test if a ray passes near a circle (ring) in 3D.
 *
 * Samples points around the circle and tests the ray against line
 * segments connecting them. This works from any viewing angle,
 * including edge-on views where a plane intersection would fail.
 */
static float ray_ring_distance_(const editor_ray_t *ray,
                                  vec3_t center, vec3_t plane_normal,
                                  float radius) {
    /* Build two perpendicular vectors in the ring plane. */
    vec3_t arbitrary = (fabsf(plane_normal.y) < 0.9f)
        ? (vec3_t){0, 1, 0}
        : (vec3_t){1, 0, 0};
    vec3_t u = vec3_normalize_safe(vec3_cross(plane_normal, arbitrary), 1e-8f);
    vec3_t v = vec3_cross(plane_normal, u);

    float step = 2.0f * 3.14159265f / (float)RING_SAMPLE_COUNT;
    float best = 1e30f;

    /* Test ray against line segments between adjacent sample points. */
    vec3_t prev;
    prev.x = center.x + u.x * radius;
    prev.y = center.y + u.y * radius;
    prev.z = center.z + u.z * radius;

    for (int i = 1; i <= RING_SAMPLE_COUNT; ++i) {
        float angle = (float)i * step;
        float ca = cosf(angle), sa = sinf(angle);
        vec3_t cur;
        cur.x = center.x + (u.x * ca + v.x * sa) * radius;
        cur.y = center.y + (u.y * ca + v.y * sa) * radius;
        cur.z = center.z + (u.z * ca + v.z * sa) * radius;

        float dist = ray_segment_distance_(ray, prev, cur);
        if (dist < best) best = dist;
        prev = cur;
    }

    return best;
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
                              float gizmo_scale) {
    if (!gizmo || !ray) return GIZMO_AXIS_NONE;

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

    if (gizmo->mode == GIZMO_MODE_ROTATE) {
        /* Test each axis ring — normal is the oriented axis direction. */
        for (int i = 0; i < 3; i++) {
            float dist = ray_ring_distance_(ray, pos, axis_dirs[i], length);
            if (dist < best_dist) {
                best_dist = dist;
                best = axis_ids[i];
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
