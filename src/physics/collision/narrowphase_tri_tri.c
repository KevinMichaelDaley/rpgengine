/**
 * @file narrowphase_tri_tri.c
 * @brief Triangle-vs-triangle narrowphase using SAT (Separating Axis Theorem).
 *
 * Tests 11 potential separating axes:
 *   - 2 face normals (one per triangle)
 *   - 9 edge cross products (3 edges × 3 edges)
 *
 * If no separating axis is found, the triangles overlap. The axis with
 * the smallest overlap gives the minimum penetration depth and contact normal.
 *
 * Non-static functions (1 / 4 limit):
 *   phys_triangle_vs_triangle
 */

#include "ferrum/physics/mesh_narrowphase.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/phys_vec3.h"

#include <math.h>
#include <string.h>

/* ---- Helpers ---- */

static phys_vec3_t cross_(phys_vec3_t a, phys_vec3_t b) {
    return (phys_vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static float dot_(phys_vec3_t a, phys_vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static phys_vec3_t sub_(phys_vec3_t a, phys_vec3_t b) {
    return (phys_vec3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

/**
 * @brief Project triangle onto axis, return min and max projections.
 */
static void project_tri_(const phys_triangle_t *tri, phys_vec3_t axis,
                           float *out_min, float *out_max) {
    float p0 = dot_(tri->v[0], axis);
    float p1 = dot_(tri->v[1], axis);
    float p2 = dot_(tri->v[2], axis);

    *out_min = p0;
    *out_max = p0;
    if (p1 < *out_min) *out_min = p1;
    if (p1 > *out_max) *out_max = p1;
    if (p2 < *out_min) *out_min = p2;
    if (p2 > *out_max) *out_max = p2;
}

/**
 * @brief Test one separating axis. Returns overlap amount (negative = separated).
 */
static float test_axis_(const phys_triangle_t *a, const phys_triangle_t *b,
                          phys_vec3_t axis) {
    float a_min, a_max, b_min, b_max;
    project_tri_(a, axis, &a_min, &a_max);
    project_tri_(b, axis, &b_min, &b_max);

    /* Overlap = min of the two possible overlap directions. */
    float overlap1 = a_max - b_min;
    float overlap2 = b_max - a_min;
    return (overlap1 < overlap2) ? overlap1 : overlap2;
}

/* ---- Public API ---- */

bool phys_triangle_vs_triangle(
    const phys_triangle_t *a,
    const phys_triangle_t *b,
    float spec_margin,
    phys_contact_point_t *contact_out)
{
    if (!a || !b || !contact_out) return false;

    /* Compute edges. */
    phys_vec3_t a_edges[3] = {
        sub_(a->v[1], a->v[0]),
        sub_(a->v[2], a->v[1]),
        sub_(a->v[0], a->v[2])
    };
    phys_vec3_t b_edges[3] = {
        sub_(b->v[1], b->v[0]),
        sub_(b->v[2], b->v[1]),
        sub_(b->v[0], b->v[2])
    };

    /* Face normals. */
    phys_vec3_t a_normal = cross_(a_edges[0], a_edges[1]);
    phys_vec3_t b_normal = cross_(b_edges[0], b_edges[1]);

    /* Collect up to 11 axes. */
    phys_vec3_t axes[11];
    int num_axes = 0;

    /* Add face normals. */
    float a_norm_len = sqrtf(dot_(a_normal, a_normal));
    float b_norm_len = sqrtf(dot_(b_normal, b_normal));
    if (a_norm_len > 1e-8f) {
        axes[num_axes++] = (phys_vec3_t){
            a_normal.x / a_norm_len,
            a_normal.y / a_norm_len,
            a_normal.z / a_norm_len
        };
    }
    if (b_norm_len > 1e-8f) {
        axes[num_axes++] = (phys_vec3_t){
            b_normal.x / b_norm_len,
            b_normal.y / b_norm_len,
            b_normal.z / b_norm_len
        };
    }

    /* Add edge cross-product axes (9 of them). */
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            phys_vec3_t c = cross_(a_edges[i], b_edges[j]);
            float len = sqrtf(dot_(c, c));
            if (len > 1e-8f) {
                axes[num_axes++] = (phys_vec3_t){
                    c.x / len, c.y / len, c.z / len
                };
            }
        }
    }

    if (num_axes == 0) return false;

    /* SAT: find the axis with minimum overlap. */
    float min_pen = 1e30f;
    phys_vec3_t best_axis = {0, 1, 0};

    for (int i = 0; i < num_axes; ++i) {
        float overlap = test_axis_(a, b, axes[i]);

        /* Negative overlap means separated along this axis. */
        if (overlap < -spec_margin) {
            return false;
        }

        if (overlap < min_pen) {
            min_pen = overlap;
            best_axis = axes[i];
        }
    }

    /* Ensure normal points from A toward B. */
    phys_vec3_t a_center = {
        (a->v[0].x + a->v[1].x + a->v[2].x) / 3.0f,
        (a->v[0].y + a->v[1].y + a->v[2].y) / 3.0f,
        (a->v[0].z + a->v[1].z + a->v[2].z) / 3.0f
    };
    phys_vec3_t b_center = {
        (b->v[0].x + b->v[1].x + b->v[2].x) / 3.0f,
        (b->v[0].y + b->v[1].y + b->v[2].y) / 3.0f,
        (b->v[0].z + b->v[1].z + b->v[2].z) / 3.0f
    };
    phys_vec3_t ab = sub_(b_center, a_center);
    if (dot_(ab, best_axis) < 0.0f) {
        best_axis.x = -best_axis.x;
        best_axis.y = -best_axis.y;
        best_axis.z = -best_axis.z;
    }

    /* Fill contact. */
    memset(contact_out, 0, sizeof(*contact_out));
    contact_out->normal = best_axis;
    contact_out->penetration = min_pen;

    /* Contact point: midpoint of the overlap region centroids. */
    contact_out->point_world = (phys_vec3_t){
        (a_center.x + b_center.x) * 0.5f,
        (a_center.y + b_center.y) * 0.5f,
        (a_center.z + b_center.z) * 0.5f
    };

    return true;
}
