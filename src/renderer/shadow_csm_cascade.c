/**
 * @file shadow_csm_cascade.c
 * @brief View-INDEPENDENT cascade fit + caster size classification (see
 *        shadow_csm.h).
 *
 * The static sun map is baked once and cached forever, and the player can roam
 * the whole scene, so the cascades must not depend on the camera (a view-fit
 * cascade would clip out a caster behind the view -- e.g. a back wall stops
 * shadowing). Instead every static caster is classified into a cascade by the
 * size of its world AABB: the largest ("background") structures fall into the
 * last, coarse cascade fit to the WHOLE scene; smaller objects fall into finer
 * cascades fit tightly to their own bounds for higher effective resolution. A
 * fragment later samples every cascade whose box contains it and unions the
 * occlusion, so shadows apply no matter where the camera looks.
 */
#include "ferrum/renderer/shadow_csm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/math/vec3.h"
#include "ferrum/math/vec4.h"

/* World-space AABB of a renderable = its local mesh AABB transformed by model. */
static void csm_world_aabb(const render_renderable_t *r, float wmin[3],
                           float wmax[3])
{
    for (int k = 0; k < 3; ++k) { wmin[k] = 1e30f; wmax[k] = -1e30f; }
    const float *lo = r->mesh->aabb_min, *hi = r->mesh->aabb_max;
    for (int c = 0; c < 8; ++c) {
        float lc[3] = { (c&1)?hi[0]:lo[0], (c&2)?hi[1]:lo[1], (c&4)?hi[2]:lo[2] };
        for (int row = 0; row < 3; ++row) {
            float w = r->model[0*4+row]*lc[0] + r->model[1*4+row]*lc[1] +
                      r->model[2*4+row]*lc[2] + r->model[3*4+row];
            if (w < wmin[row]) wmin[row] = w;
            if (w > wmax[row]) wmax[row] = w;
        }
    }
}

/* Diagonal length of a world AABB (the caster's "size"). */
static float csm_aabb_diag(const float wmin[3], const float wmax[3])
{
    float dx = wmax[0]-wmin[0], dy = wmax[1]-wmin[1], dz = wmax[2]-wmin[2];
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

uint32_t shadow_csm_cascade_of(const shadow_csm_t *csm,
                               const render_renderable_t *r)
{
    if (csm == NULL || r == NULL || r->mesh == NULL)
        return 0;
    /* Explicit per-caster tag wins (lets a caller force a cascade). */
    if (r->shadow_cascade >= 0 && (uint32_t)r->shadow_cascade < csm->cascades)
        return (uint32_t)r->shadow_cascade;
    if (csm->cascades <= 1 || csm->size_log_max <= csm->size_log_min)
        return csm->cascades ? csm->cascades - 1u : 0u;
    /* Log-size -> [0, cascades-1]: small casters to the fine cascade 0, large
     * "background" casters to the coarse last cascade. */
    float wmin[3], wmax[3];
    csm_world_aabb(r, wmin, wmax);
    float d = csm_aabb_diag(wmin, wmax);
    float lt = logf(d > 1e-6f ? d : 1e-6f);
    float t = (lt - csm->size_log_min) / (csm->size_log_max - csm->size_log_min);
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    int ci = (int)(t * (float)(csm->cascades - 1u) + 0.5f);
    if (ci < 0) ci = 0;
    if ((uint32_t)ci >= csm->cascades) ci = (int)csm->cascades - 1;
    return (uint32_t)ci;
}

/* Fit a texel-snapped orthographic light matrix to the light-space AABB of the 8
 * world points @p sc (a cascade's caster box). The X/Y box is clamped to the
 * scene so a stray margin never wastes resolution; the NEAR plane is pulled back
 * toward the light past the most-upstream scene caster so tall casters above the
 * receivers still write into the map. @p scene (8 scene-AABB corners, or NULL)
 * drives that clamp/extension. */
static void csm_fit_ortho(const vec3_t sc[8], vec3_t dir, vec3_t up, float res,
                          const vec3_t *scene,
                          mat4_t *vp_out, float eye_out[3], float *far_out,
                          float *texel_world_out)
{
    vec3_t centroid = { 0, 0, 0 };
    for (int j = 0; j < 8; ++j) centroid = vec3_add(centroid, sc[j]);
    centroid = vec3_scale(centroid, 1.0f / 8.0f);

    /* Eye far enough back along -dir that every box AND scene caster is in front
     * (parallel projection: the eye distance only offsets depth, not the box). */
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

    /* Tight light-space AABB of the box corners. */
    float lmin[3] = { 1e30f, 1e30f, 1e30f }, lmax[3] = { -1e30f, -1e30f, -1e30f };
    for (int j = 0; j < 8; ++j) {
        vec4_t p = mat4_mul_vec4(lview, (vec4_t){ sc[j].x, sc[j].y, sc[j].z, 1.0f });
        float v[3] = { p.x, p.y, p.z };
        for (int k = 0; k < 3; ++k) { if (v[k] < lmin[k]) lmin[k] = v[k]; if (v[k] > lmax[k]) lmax[k] = v[k]; }
    }
    /* Clamp X/Y to the scene bounds and extend the near plane (larger z) to the
     * scene's upstream extent, so tall/background casters between the light and
     * the receivers still render. */
    if (scene) {
        float smin[3] = { 1e30f,1e30f,1e30f }, smax[3] = { -1e30f,-1e30f,-1e30f };
        for (int j = 0; j < 8; ++j) {
            vec4_t p = mat4_mul_vec4(lview, (vec4_t){ scene[j].x, scene[j].y, scene[j].z, 1.0f });
            float v[3] = { p.x, p.y, p.z };
            for (int k = 0; k < 3; ++k) { if (v[k] < smin[k]) smin[k] = v[k]; if (v[k] > smax[k]) smax[k] = v[k]; }
        }
        for (int k = 0; k < 2; ++k) {              /* X/Y: keep inside the scene */
            if (smin[k] > lmin[k]) lmin[k] = smin[k];
            if (smax[k] < lmax[k]) lmax[k] = smax[k];
        }
        if (smax[2] > lmax[2]) lmax[2] = smax[2];  /* near: enclose upstream casters */
        if (smin[2] > lmin[2]) lmin[2] = smin[2];  /* far: don't render past the scene */
    }
    for (int k = 0; k < 3; ++k) if (lmax[k] <= lmin[k]) lmax[k] = lmin[k] + 0.01f;
    float nearp = -lmax[2], farp = -lmin[2];
    if (nearp < 0.01f) nearp = 0.01f;

    /* Texel-snap the X/Y origin so shadows don't shimmer. */
    float wx = lmax[0] - lmin[0], wy = lmax[1] - lmin[1];
    float tx = wx / res, ty = wy / res;
    if (tx > 1e-8f) lmin[0] = floorf(lmin[0] / tx) * tx;
    if (ty > 1e-8f) lmin[1] = floorf(lmin[1] / ty) * ty;
    lmax[0] = lmin[0] + wx; lmax[1] = lmin[1] + wy;

    mat4_t lproj = mat4_ortho(lmin[0], lmax[0], lmin[1], lmax[1], nearp, farp);
    *vp_out = mat4_mul(lproj, lview);
    eye_out[0] = eye.x; eye_out[1] = eye.y; eye_out[2] = eye.z;
    *far_out = farp;
    /* World size of one LOD-0 texel (mean of X/Y), so a world-space penumbra maps
     * to this cascade's mip LOD and cross-cascade samples align. */
    if (texel_world_out)
        *texel_world_out = 0.5f * (wx + wy) / res;
}

/* The 8 corners of an AABB as vec3s. */
static void csm_box_corners(const float mn[3], const float mx[3], vec3_t out[8])
{
    for (int c = 0; c < 8; ++c)
        out[c] = (vec3_t){ (c&1)?mx[0]:mn[0], (c&2)?mx[1]:mn[1], (c&4)?mx[2]:mn[2] };
}

void shadow_csm_update(shadow_csm_t *csm, const render_scene_t *scene,
                       const float light_dir[3],
                       const float scene_min[3], const float scene_max[3])
{
    if (csm == NULL || scene == NULL || light_dir == NULL)
        return;
    /* Cached forever: view-independent, so the player moving never invalidates
     * it. The owner clears static_valid on a scene/light change to force a refit. */
    if (csm->static_valid)
        return;

    vec3_t dir = vec3_normalize_safe((vec3_t){ light_dir[0], light_dir[1],
                                               light_dir[2] }, 1e-6f);
    if (dir.x == 0.0f && dir.y == 0.0f && dir.z == 0.0f)
        dir = (vec3_t){ 0.0f, -1.0f, 0.0f };
    vec3_t up = (fabsf(dir.y) < 0.99f) ? (vec3_t){ 0, 1, 0 } : (vec3_t){ 0, 0, 1 };

    uint32_t to = scene->dynamic_from;
    if (to > scene->count) to = scene->count;

    /* --- Scene AABB (from the caller, or unioned from the static casters). --- */
    float smin[3], smax[3];
    int have_scene = (scene_min != NULL && scene_max != NULL &&
                      scene_max[0] > scene_min[0] && scene_max[1] > scene_min[1] &&
                      scene_max[2] > scene_min[2]);
    if (have_scene) {
        for (int k = 0; k < 3; ++k) { smin[k] = scene_min[k]; smax[k] = scene_max[k]; }
    } else {
        for (int k = 0; k < 3; ++k) { smin[k] = 1e30f; smax[k] = -1e30f; }
        for (uint32_t i = 0; i < to; ++i) {
            if (scene->items[i].mesh == NULL) continue;
            float wmn[3], wmx[3]; csm_world_aabb(&scene->items[i], wmn, wmx);
            for (int k = 0; k < 3; ++k) {
                if (wmn[k] < smin[k]) smin[k] = wmn[k];
                if (wmx[k] > smax[k]) smax[k] = wmx[k];
            }
        }
        if (smax[0] <= smin[0]) { for (int k=0;k<3;++k){ smin[k]=-1.0f; smax[k]=1.0f; } }
    }
    float scene_diag = csm_aabb_diag(smin, smax);
    vec3_t scene_c[8]; csm_box_corners(smin, smax, scene_c);

    /* --- Size range over the static casters (drives the classification). --- */
    csm->size_log_min = 1e30f; csm->size_log_max = -1e30f;
    for (uint32_t i = 0; i < to; ++i) {
        if (scene->items[i].mesh == NULL) continue;
        float wmn[3], wmx[3]; csm_world_aabb(&scene->items[i], wmn, wmx);
        float lt = logf(fmaxf(csm_aabb_diag(wmn, wmx), 1e-6f));
        if (lt < csm->size_log_min) csm->size_log_min = lt;
        if (lt > csm->size_log_max) csm->size_log_max = lt;
    }
    if (csm->size_log_max < csm->size_log_min) { /* no casters */
        csm->size_log_min = 0.0f; csm->size_log_max = 0.0f;
    }

    /* --- Per-cascade world AABB of the casters classified into it. --- */
    float cmin[SHADOW_CSM_MAX_CASCADES][3], cmax[SHADOW_CSM_MAX_CASCADES][3];
    int   chas[SHADOW_CSM_MAX_CASCADES] = { 0 };
    for (uint32_t c = 0; c < csm->cascades; ++c)
        for (int k = 0; k < 3; ++k) { cmin[c][k] = 1e30f; cmax[c][k] = -1e30f; }
    for (uint32_t i = 0; i < to; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL) continue;
        uint32_t ci = shadow_csm_cascade_of(csm, r);
        float wmn[3], wmx[3]; csm_world_aabb(r, wmn, wmx);
        for (int k = 0; k < 3; ++k) {
            if (wmn[k] < cmin[ci][k]) cmin[ci][k] = wmn[k];
            if (wmx[k] > cmax[ci][k]) cmax[ci][k] = wmx[k];
        }
        chas[ci] = 1;
    }

    /* --- Fit each cascade. Coarse last cascade = whole scene (background);
     * finer cascades = their caster box + a shadow-throw margin, clamped to the
     * scene by csm_fit_ortho. Empty cascades get a tiny valid (all-far) box. --- */
    float margin = 0.10f * scene_diag;
    for (uint32_t c = 0; c < csm->cascades; ++c) {
        float bmin[3], bmax[3];
        if (c == csm->cascades - 1u || !chas[c]) {
            /* Background / empty: cover the whole scene so every receiver can
             * sample it (empty ones stay all-far, contributing no occlusion). */
            for (int k = 0; k < 3; ++k) { bmin[k] = smin[k]; bmax[k] = smax[k]; }
        } else {
            for (int k = 0; k < 3; ++k) {
                bmin[k] = cmin[c][k] - margin;
                bmax[k] = cmax[c][k] + margin;
            }
        }
        vec3_t sc[8]; csm_box_corners(bmin, bmax, sc);
        csm_fit_ortho(sc, dir, up, (float)csm->static_res, scene_c,
                      &csm->view_proj[c], csm->eye[c], &csm->far_plane[c],
                      &csm->texel_world[c]);
        if (getenv("CSM_DEBUG"))
            fprintf(stderr, "  cascade %u: %s box(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f) far=%.1f\n",
                    c, (c==csm->cascades-1u)?"scene":(chas[c]?"casters":"empty"),
                    bmin[0],bmin[1],bmin[2], bmax[0],bmax[1],bmax[2], csm->far_plane[c]);
    }

    /* --- Single dynamic map: fit to the whole scene so a dynamic caster
     * anywhere lands in it. --- */
    csm_fit_ortho(scene_c, dir, up, (float)csm->dynamic_res, scene_c,
                  &csm->dyn_view_proj, csm->dyn_eye, &csm->dyn_far, NULL);
}
