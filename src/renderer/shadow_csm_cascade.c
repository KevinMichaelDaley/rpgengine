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
#include <stdio.h>
#include <stdlib.h>

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

/* Fit a texel-snapped orthographic light matrix to the TIGHT light-space AABB of
 * this cascade's 8 frustum-slice corners (a slice's bounding SPHERE is far larger
 * than the slice and makes adjacent cascades overlap -- so we use the AABB). The
 * X/Y box is tight to the slice (each cascade covers a distinct region); only the
 * NEAR plane is pulled back toward the light, past the most-upstream @p scene
 * caster, so tall casters (a vault above the slice) still write into the map. */
static void csm_fit_ortho(const vec3_t sc[8], vec3_t dir, vec3_t up, float res,
                          const vec3_t *scene,
                          mat4_t *vp_out, float eye_out[3], float *far_out)
{
    vec3_t centroid = { 0, 0, 0 };
    for (int j = 0; j < 8; ++j) centroid = vec3_add(centroid, sc[j]);
    centroid = vec3_scale(centroid, 1.0f / 8.0f);

    /* Place the eye far enough back along -dir that every slice AND scene caster
     * is in front of it (parallel projection: the eye's distance only offsets
     * depth, not the X/Y box). */
    float back = 0.0f;
    for (int j = 0; j < 8; ++j) {
        float t = vec3_dot(vec3_sub(centroid, sc[j]), dir);
        if (t > back) back = t;
    }
    if (scene)
        for (int j = 0; j < 8; ++j) {
            float t = vec3_dot(vec3_sub(centroid, scene[j]), dir);
            if (t > back) back = t;
        }
    back += 1.0f;
    vec3_t eye = vec3_sub(centroid, vec3_scale(dir, back));
    mat4_t lview;
    mat4_look_at(eye, centroid, up, &lview);

    /* Tight light-space AABB of the slice corners. */
    float lmin[3] = { 1e30f, 1e30f, 1e30f }, lmax[3] = { -1e30f, -1e30f, -1e30f };
    for (int j = 0; j < 8; ++j) {
        vec4_t p = mat4_mul_vec4(lview, (vec4_t){ sc[j].x, sc[j].y, sc[j].z, 1.0f });
        float v[3] = { p.x, p.y, p.z };
        for (int k = 0; k < 3; ++k) { if (v[k] < lmin[k]) lmin[k] = v[k]; if (v[k] > lmax[k]) lmax[k] = v[k]; }
    }
    /* Shrink the box to the geometry actually in this cascade = the frustum slice
     * INTERSECTED with the scene bounds (both in light space). Otherwise a far
     * cascade's box is the huge empty slice and the 32m of geometry clumps into
     * the middle of the 4096^2 map, wasting resolution. X/Y clamp to the scene;
     * the near plane (larger z) still extends to the scene top for tall casters. */
    if (scene) {
        float smin[3] = { 1e30f,1e30f,1e30f }, smax[3] = { -1e30f,-1e30f,-1e30f };
        for (int j = 0; j < 8; ++j) {
            vec4_t p = mat4_mul_vec4(lview, (vec4_t){ scene[j].x, scene[j].y, scene[j].z, 1.0f });
            float v[3] = { p.x, p.y, p.z };
            for (int k = 0; k < 3; ++k) { if (v[k] < smin[k]) smin[k] = v[k]; if (v[k] > smax[k]) smax[k] = v[k]; }
        }
        for (int k = 0; k < 2; ++k) {              /* X/Y: intersect slice with scene */
            if (smin[k] > lmin[k]) lmin[k] = smin[k];
            if (smax[k] < lmax[k]) lmax[k] = smax[k];
        }
        if (smax[2] > lmax[2]) lmax[2] = smax[2];  /* near: enclose upstream casters */
        if (smin[2] > lmin[2]) lmin[2] = smin[2];  /* far: don't render past the scene */
    }
    /* Empty intersection -> this cascade sees no geometry; keep a tiny valid box
     * (its meshes are all culled anyway). */
    for (int k = 0; k < 3; ++k) if (lmax[k] <= lmin[k]) lmax[k] = lmin[k] + 0.01f;
    float nearp = -lmax[2], farp = -lmin[2];
    if (nearp < 0.01f) nearp = 0.01f;

    /* Texel-snap the X/Y box origin so shadows don't shimmer as the camera moves. */
    float wx = lmax[0] - lmin[0], wy = lmax[1] - lmin[1];
    float tx = wx / res, ty = wy / res;
    if (tx > 1e-8f) lmin[0] = floorf(lmin[0] / tx) * tx;
    if (ty > 1e-8f) lmin[1] = floorf(lmin[1] / ty) * ty;
    lmax[0] = lmin[0] + wx; lmax[1] = lmin[1] + wy;

    mat4_t lproj = mat4_ortho(lmin[0], lmax[0], lmin[1], lmax[1], nearp, farp);
    *vp_out = mat4_mul(lproj, lview);
    eye_out[0] = eye.x; eye_out[1] = eye.y; eye_out[2] = eye.z;
    *far_out = farp;
    if (getenv("CSM_DEBUG"))
        fprintf(stderr, "  fit: box %.1f x %.1f  depth[%.1f,%.1f]  back=%.1f\n",
                lmax[0]-lmin[0], lmax[1]-lmin[1], nearp, farp, back);
}

void shadow_csm_update(shadow_csm_t *csm, const render_camera_t *camera,
                       const float light_dir[3],
                       const float scene_min[3], const float scene_max[3])
{
    if (csm == NULL || camera == NULL || light_dir == NULL)
        return;
    /* Cache the cascade matrices FOREVER once baked: the sun is static and the
     * player barely moves, so re-fitting to the camera every frame (and the
     * re-bake it triggers) is wasted work. The owner invalidates explicitly
     * (static_valid = false) on a zone change / teleport. */
    if (csm->static_valid)
        return;
    /* When the whole-scene AABB is given, every cascade is fit to the ENTIRE
     * scene so no caster (tall vaults, geometry behind the view) is ever clipped
     * out of the shadow map. The 8 scene corners drive csm_fit_ortho below. */
    int fit_scene = (scene_min != NULL && scene_max != NULL &&
                     scene_max[0] > scene_min[0] && scene_max[1] > scene_min[1] &&
                     scene_max[2] > scene_min[2]);
    vec3_t scene_corners[8];
    if (fit_scene)
        for (int i = 0; i < 8; ++i)
            scene_corners[i] = (vec3_t){ (i&1)?scene_max[0]:scene_min[0],
                                         (i&2)?scene_max[1]:scene_min[1],
                                         (i&4)?scene_max[2]:scene_min[2] };
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

        /* Interpolate the frustum edges to this slice's 8 corners, then fit. Each
         * cascade covers ONLY its depth slice (nested CSM), so the XY box stays
         * tight for resolution; caster inclusion is handled by extending the near
         * plane toward the light with the scene AABB (see csm_fit_ortho). */
        vec3_t sc[8];
        for (int j = 0; j < 4; ++j) {
            sc[j] = vec3_lerp(corners[j], corners[j + 4], tn);
            sc[j + 4] = vec3_lerp(corners[j], corners[j + 4], tf);
        }
        mat4_t vp;
        float eye3[3], far_plane;
        csm_fit_ortho(sc, dir, up, (float)csm->static_res,
                      fit_scene ? scene_corners : NULL, &vp, eye3, &far_plane);
        /* (Cached forever after the first bake -- see the early-out above.) */
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
    csm_fit_ortho(dsc, dir, up, (float)csm->dynamic_res,
                  fit_scene ? scene_corners : NULL, &csm->dyn_view_proj,
                  csm->dyn_eye, &csm->dyn_far);
}
