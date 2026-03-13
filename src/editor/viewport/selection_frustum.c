/**
 * @file selection_frustum.c
 * @brief Frustum extraction and frustum-AABB intersection test.
 *
 * Non-static functions: 2 (editor_frustum_from_camera,
 *                          frustum_intersect_aabb).
 */

#include "ferrum/editor/viewport/selection_raycast.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/math/mat4.h"
#include <math.h>

/** @brief Normalize a frustum plane in-place. */
static void normalize_plane_(float plane[4]) {
    float len = sqrtf(plane[0] * plane[0] + plane[1] * plane[1] +
                       plane[2] * plane[2]);
    if (len > 1e-8f) {
        float inv = 1.0f / len;
        plane[0] *= inv;
        plane[1] *= inv;
        plane[2] *= inv;
        plane[3] *= inv;
    }
}

int editor_frustum_from_camera(const struct editor_camera *cam,
                                float aspect, editor_frustum_t *out) {
    if (!cam || !out) return -1;

    mat4_t view, proj;
    if (editor_camera_view_matrix(cam, &view) != 0) return -1;
    if (editor_camera_projection_matrix(cam, aspect, &proj) != 0) return -1;

    /* VP = proj * view. */
    mat4_t vp = mat4_mul(proj, view);
    const float *m = vp.m;

    /* Extract planes from the view-projection matrix.
     * Column-major: row i of VP is: m[i], m[i+4], m[i+8], m[i+12]. */

    /* Left:   row3 + row0 */
    out->planes[0][0] = m[3]  + m[0];
    out->planes[0][1] = m[7]  + m[4];
    out->planes[0][2] = m[11] + m[8];
    out->planes[0][3] = m[15] + m[12];
    normalize_plane_(out->planes[0]);

    /* Right:  row3 - row0 */
    out->planes[1][0] = m[3]  - m[0];
    out->planes[1][1] = m[7]  - m[4];
    out->planes[1][2] = m[11] - m[8];
    out->planes[1][3] = m[15] - m[12];
    normalize_plane_(out->planes[1]);

    /* Bottom: row3 + row1 */
    out->planes[2][0] = m[3]  + m[1];
    out->planes[2][1] = m[7]  + m[5];
    out->planes[2][2] = m[11] + m[9];
    out->planes[2][3] = m[15] + m[13];
    normalize_plane_(out->planes[2]);

    /* Top:    row3 - row1 */
    out->planes[3][0] = m[3]  - m[1];
    out->planes[3][1] = m[7]  - m[5];
    out->planes[3][2] = m[11] - m[9];
    out->planes[3][3] = m[15] - m[13];
    normalize_plane_(out->planes[3]);

    /* Near:   row3 + row2 */
    out->planes[4][0] = m[3]  + m[2];
    out->planes[4][1] = m[7]  + m[6];
    out->planes[4][2] = m[11] + m[10];
    out->planes[4][3] = m[15] + m[14];
    normalize_plane_(out->planes[4]);

    /* Far:    row3 - row2 */
    out->planes[5][0] = m[3]  - m[2];
    out->planes[5][1] = m[7]  - m[6];
    out->planes[5][2] = m[11] - m[10];
    out->planes[5][3] = m[15] - m[14];
    normalize_plane_(out->planes[5]);

    return 0;
}

bool frustum_intersect_aabb(const editor_frustum_t *frustum,
                             vec3_t aabb_min, vec3_t aabb_max) {
    if (!frustum) return false;

    /* Test each plane. If the AABB is fully outside any plane, reject. */
    for (int i = 0; i < 6; i++) {
        const float *p = frustum->planes[i];

        /* Find the positive vertex (furthest along the plane normal). */
        float px = (p[0] >= 0.0f) ? aabb_max.x : aabb_min.x;
        float py = (p[1] >= 0.0f) ? aabb_max.y : aabb_min.y;
        float pz = (p[2] >= 0.0f) ? aabb_max.z : aabb_min.z;

        /* If the positive vertex is outside, the AABB is fully outside. */
        if (p[0] * px + p[1] * py + p[2] * pz + p[3] < 0.0f) {
            return false;
        }
    }

    return true;
}
