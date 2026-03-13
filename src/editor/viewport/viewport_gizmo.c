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
static const float AXIS_HIT_RADIUS = 0.08f;

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
    gizmo->position = (vec3_t){0.0f, 0.0f, 0.0f};
    gizmo->dragging = false;
}

void gizmo_state_set_mode(gizmo_state_t *gizmo, gizmo_mode_t mode) {
    gizmo->mode = mode;
    gizmo->active_axis = GIZMO_AXIS_NONE;
    gizmo->dragging = false;
}

gizmo_axis_t gizmo_hit_test(const gizmo_state_t *gizmo,
                              const struct editor_ray *ray,
                              float gizmo_scale) {
    if (!gizmo || !ray) return GIZMO_AXIS_NONE;

    float length = ARROW_LENGTH * gizmo_scale;
    float threshold = AXIS_HIT_RADIUS * gizmo_scale;
    vec3_t pos = gizmo->position;

    /* Test each axis arrow as a line segment. */
    struct {
        gizmo_axis_t axis;
        vec3_t end;
    } axes[3] = {
        {GIZMO_AXIS_X, {pos.x + length, pos.y, pos.z}},
        {GIZMO_AXIS_Y, {pos.x, pos.y + length, pos.z}},
        {GIZMO_AXIS_Z, {pos.x, pos.y, pos.z + length}},
    };

    gizmo_axis_t best = GIZMO_AXIS_NONE;
    float best_dist = threshold;

    for (int i = 0; i < 3; i++) {
        float dist = ray_segment_distance_(ray, pos, axes[i].end);
        if (dist < best_dist) {
            best_dist = dist;
            best = axes[i].axis;
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
