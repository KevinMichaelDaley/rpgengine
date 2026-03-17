/**
 * @file snap_ray_triangle.c
 * @brief Ray-triangle and ray-mesh intersection for surface snap.
 *
 * Provides Möller–Trumbore ray-triangle test without the t∈[0,1]
 * constraint used by the physics CCD, allowing hits at any distance.
 *
 * Non-static functions (2 / 4 limit):
 *   snap_ray_vs_triangle
 *   snap_ray_vs_snap_mesh
 */

#include "ferrum/editor/viewport/snap/snap_raycast.h"
#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/vec4.h"
#include "ferrum/math/mat4.h"

#include <math.h>

/** @brief Transform a position by a 4x4 model matrix. */
static vec3_t transform_point_(const mat4_t *m, vec3_t p) {
    vec4_t v = {p.x, p.y, p.z, 1.0f};
    vec4_t r = mat4_mul_vec4(*m, v);
    return (vec3_t){r.x, r.y, r.z};
}

/** @brief Transform a direction (normal) by a 4x4 model matrix (upper 3x3). */
static vec3_t transform_normal_(const mat4_t *m, vec3_t n) {
    /* For non-uniform scale, this should use the inverse-transpose.
     * For now, use upper 3x3 and re-normalize. This is correct for
     * uniform scale and rotation-only transforms. */
    vec3_t r = {
        m->m[0] * n.x + m->m[4] * n.y + m->m[8]  * n.z,
        m->m[1] * n.x + m->m[5] * n.y + m->m[9]  * n.z,
        m->m[2] * n.x + m->m[6] * n.y + m->m[10] * n.z,
    };
    float len = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z);
    if (len > 1e-8f) {
        float inv = 1.0f / len;
        r.x *= inv; r.y *= inv; r.z *= inv;
    }
    return r;
}

bool snap_ray_vs_triangle(vec3_t origin, vec3_t dir,
                            vec3_t v0, vec3_t v1, vec3_t v2,
                            float *t_out) {
    if (!t_out) return false;

    const float EPSILON = 1e-7f;

    vec3_t e1 = vec3_sub(v1, v0);
    vec3_t e2 = vec3_sub(v2, v0);
    vec3_t h = vec3_cross(dir, e2);
    float a = vec3_dot(e1, h);

    if (fabsf(a) < EPSILON) return false;

    float f = 1.0f / a;
    vec3_t s = vec3_sub(origin, v0);
    float u = f * vec3_dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    vec3_t q = vec3_cross(s, e1);
    float v = f * vec3_dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = f * vec3_dot(e2, q);
    if (t < 0.0f) return false;  /* Behind ray origin. */
    /* No upper bound on t — any distance is valid. */

    *t_out = t;
    return true;
}

bool snap_ray_vs_snap_mesh(vec3_t origin, vec3_t dir,
                             const snap_mesh_t *mesh,
                             const mat4_t *model,
                             float *out_t, uint32_t *out_face,
                             vec3_t *out_normal) {
    if (!mesh || !model || !out_t || !out_face || !out_normal) return false;
    if (!mesh->positions || !mesh->indices) return false;

    uint32_t tri_count = mesh->index_count / 3;
    float best_t = 1e30f;
    uint32_t best_face = 0;
    bool found = false;

    for (uint32_t fi = 0; fi < tri_count; fi++) {
        uint32_t i0 = mesh->indices[fi * 3 + 0];
        uint32_t i1 = mesh->indices[fi * 3 + 1];
        uint32_t i2 = mesh->indices[fi * 3 + 2];

        /* Transform vertices to world space. */
        vec3_t lv0 = {mesh->positions[i0 * 3 + 0],
                      mesh->positions[i0 * 3 + 1],
                      mesh->positions[i0 * 3 + 2]};
        vec3_t lv1 = {mesh->positions[i1 * 3 + 0],
                      mesh->positions[i1 * 3 + 1],
                      mesh->positions[i1 * 3 + 2]};
        vec3_t lv2 = {mesh->positions[i2 * 3 + 0],
                      mesh->positions[i2 * 3 + 1],
                      mesh->positions[i2 * 3 + 2]};

        vec3_t wv0 = transform_point_(model, lv0);
        vec3_t wv1 = transform_point_(model, lv1);
        vec3_t wv2 = transform_point_(model, lv2);

        float t;
        if (snap_ray_vs_triangle(origin, dir, wv0, wv1, wv2, &t)) {
            if (t < best_t) {
                best_t = t;
                best_face = fi;
                found = true;
            }
        }
    }

    if (!found) return false;

    *out_t = best_t;
    *out_face = best_face;

    /* Compute face normal from the hit triangle's world-space vertices. */
    uint32_t i0 = mesh->indices[best_face * 3 + 0];
    uint32_t i1 = mesh->indices[best_face * 3 + 1];
    uint32_t i2 = mesh->indices[best_face * 3 + 2];

    vec3_t lv0 = {mesh->positions[i0 * 3], mesh->positions[i0 * 3 + 1],
                  mesh->positions[i0 * 3 + 2]};
    vec3_t lv1 = {mesh->positions[i1 * 3], mesh->positions[i1 * 3 + 1],
                  mesh->positions[i1 * 3 + 2]};
    vec3_t lv2 = {mesh->positions[i2 * 3], mesh->positions[i2 * 3 + 1],
                  mesh->positions[i2 * 3 + 2]};

    vec3_t wv0 = transform_point_(model, lv0);
    vec3_t wv1 = transform_point_(model, lv1);
    vec3_t wv2 = transform_point_(model, lv2);

    vec3_t e1 = vec3_sub(wv1, wv0);
    vec3_t e2 = vec3_sub(wv2, wv0);
    vec3_t n = vec3_cross(e1, e2);
    float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len > 1e-8f) {
        float inv = 1.0f / len;
        n.x *= inv; n.y *= inv; n.z *= inv;
    }

    *out_normal = n;
    return true;
}
