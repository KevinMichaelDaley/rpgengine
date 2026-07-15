/**
 * @file shadow_csm_cascade.c
 * @brief Cascade split + stabilized light-matrix setup (see shadow_csm.h).
 *
 * For each cascade we intersect the camera sub-frustum (a depth slice) with a
 * bounding sphere, then fit a light-space orthographic box to that sphere and
 * texel-snap it so the shadow does not shimmer as the camera moves. The sphere
 * (rather than the raw AABB of the corners) keeps the fit rotation-invariant.
 */
#include "ferrum/renderer/shadow_csm.h"

#include <math.h>

#include "ferrum/math/vec3.h"
#include "ferrum/math/vec4.h"

/* Unproject the 8 NDC-cube corners through inv(proj*view) into world space. */
static void csm_frustum_corners(const render_camera_t *cam, vec3_t out[8])
{
    mat4_t view = { { 0 } }, proj = { { 0 } }, vp, inv;
    for (int i = 0; i < 16; ++i) { view.m[i] = cam->view[i]; proj.m[i] = cam->proj[i]; }
    vp = mat4_mul(proj, view);
    if (mat4_inverse(vp, &inv) != 0) { /* 0 = success. */
        for (int i = 0; i < 8; ++i) out[i] = (vec3_t){ 0, 0, 0 };
        return;
    }
    int k = 0;
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                vec4_t ndc = { x ? 1.0f : -1.0f, y ? 1.0f : -1.0f,
                               z ? 1.0f : -1.0f, 1.0f };
                vec4_t w = mat4_mul_vec4(inv, ndc);
                float iw = (w.w != 0.0f) ? 1.0f / w.w : 0.0f;
                out[k++] = (vec3_t){ w.x * iw, w.y * iw, w.z * iw };
            }
}

/* Recover near/far from a GL perspective proj: m[10]=-(f+n)/(f-n), m[14]=-2fn/(f-n). */
static void csm_near_far(const float proj[16], float *near_out, float *far_out)
{
    float c = proj[10], d = proj[14];
    *near_out = d / (c - 1.0f);
    *far_out = d / (c + 1.0f);
}

/* Fit a texel-snapped orthographic light matrix to the bounding sphere of 8
 * world-space corners, viewed along `dir`. Stabilised: the radius is rounded to
 * a fixed increment and the centre snapped to the `res` texel grid. */
static void csm_fit_ortho(const vec3_t sc[8], vec3_t dir, vec3_t up, float res,
                          mat4_t *vp_out, float eye_out[3], float *far_out)
{
    vec3_t centroid = { 0, 0, 0 };
    for (int j = 0; j < 8; ++j) centroid = vec3_add(centroid, sc[j]);
    centroid = vec3_scale(centroid, 1.0f / 8.0f);
    float radius = 0.0f;
    for (int j = 0; j < 8; ++j) {
        float d = vec3_distance(centroid, sc[j]);
        if (d > radius) radius = d;
    }
    if (radius < 1e-4f) radius = 1e-4f;
    radius = ceilf(radius * 16.0f) / 16.0f;

    vec3_t eye = vec3_sub(centroid, vec3_scale(dir, 2.0f * radius));
    float far_plane = 4.0f * radius;
    mat4_t lview;
    mat4_look_at(eye, centroid, up, &lview);
    mat4_t lproj = mat4_ortho(-radius, radius, -radius, radius, 0.0f, far_plane);
    mat4_t vp = mat4_mul(lproj, lview);
    vec4_t o = mat4_mul_vec4(vp, (vec4_t){ centroid.x, centroid.y, centroid.z, 1.0f });
    float ow = (o.w != 0.0f) ? 1.0f / o.w : 1.0f;
    float tx = o.x * ow * 0.5f * res, ty = o.y * ow * 0.5f * res;
    lproj.m[12] += (roundf(tx) - tx) * 2.0f / res;
    lproj.m[13] += (roundf(ty) - ty) * 2.0f / res;
    *vp_out = mat4_mul(lproj, lview);
    eye_out[0] = eye.x; eye_out[1] = eye.y; eye_out[2] = eye.z;
    *far_out = far_plane;
}

void shadow_csm_update(shadow_csm_t *csm, const render_camera_t *camera,
                       const float light_dir[3])
{
    if (csm == NULL || camera == NULL || light_dir == NULL)
        return;
    vec3_t dir = vec3_normalize_safe((vec3_t){ light_dir[0], light_dir[1],
                                               light_dir[2] }, 1e-6f);
    if (dir.x == 0.0f && dir.y == 0.0f && dir.z == 0.0f)
        dir = (vec3_t){ 0.0f, -1.0f, 0.0f };
    vec3_t up = (fabsf(dir.y) < 0.99f) ? (vec3_t){ 0, 1, 0 } : (vec3_t){ 0, 0, 1 };

    /* Ordering from csm_frustum_corners: bit0=x, bit1=y, bit2=z(near/far).
     * Corners 0..3 are the near plane (z=-1), 4..7 the far plane (z=+1). */
    vec3_t corners[8];
    csm_frustum_corners(camera, corners);
    float near_p, far_p;
    csm_near_far(camera->proj, &near_p, &far_p);
    float far_cam = far_p; /* uncapped camera far (the far corners sit here). */
    /* Cap the shadowed range so texels stay fine over the scene rather than the
     * whole (possibly huge) camera far plane. */
    if (csm->max_distance > 0.0f && far_p > near_p + csm->max_distance)
        far_p = near_p + csm->max_distance;
    float range = far_p - near_p;
    if (range <= 0.0f) range = 1.0f;

    for (uint32_t c = 0; c < csm->cascades; ++c) {
        /* Practical split scheme (log/uniform blend) for this cascade's near/far
         * fraction along the frustum depth. */
        float fn = (float)c / (float)csm->cascades;
        float ff = (float)(c + 1) / (float)csm->cascades;
        float uni_n = near_p + range * fn, uni_f = near_p + range * ff;
        float log_n = near_p * powf(far_p / near_p, fn);
        float log_f = near_p * powf(far_p / near_p, ff);
        float dn = csm->lambda * log_n + (1.0f - csm->lambda) * uni_n;
        float df = csm->lambda * log_f + (1.0f - csm->lambda) * uni_f;
        float tn = (dn - near_p) / range, tf = (df - near_p) / range;
        csm->split_view[c] = df;

        /* Interpolate the frustum edges to this slice's 8 corners, then fit. */
        vec3_t sc[8];
        for (int j = 0; j < 4; ++j) {
            sc[j] = vec3_lerp(corners[j], corners[j + 4], tn);
            sc[j + 4] = vec3_lerp(corners[j], corners[j + 4], tf);
        }
        mat4_t vp;
        float eye3[3], far_plane;
        csm_fit_ortho(sc, dir, up, (float)csm->static_res, &vp, eye3, &far_plane);

        /* Invalidate the static cache if this cascade's matrix moved. */
        for (int i = 0; i < 16 && csm->static_valid; ++i)
            if (fabsf(vp.m[i] - csm->view_proj[c].m[i]) > 1e-5f)
                csm->static_valid = false;

        csm->view_proj[c] = vp;
        csm->eye[c][0] = eye3[0]; csm->eye[c][1] = eye3[1]; csm->eye[c][2] = eye3[2];
        csm->far_plane[c] = far_plane;
    }

    /* Single dynamic-caster ortho fit to the whole capped shadowed frustum, so a
     * dynamic object anywhere in the shadowed range lands in the one small map.
     * Not texel-snapped to a cache -- it is re-rendered every frame anyway. */
    float t_far = (far_cam > near_p) ? (far_p - near_p) / (far_cam - near_p) : 1.0f;
    vec3_t dsc[8];
    for (int j = 0; j < 4; ++j) {
        dsc[j] = corners[j];
        dsc[j + 4] = vec3_lerp(corners[j], corners[j + 4], t_far);
    }
    csm_fit_ortho(dsc, dir, up, (float)csm->dynamic_res, &csm->dyn_view_proj,
                  csm->dyn_eye, &csm->dyn_far);
}
