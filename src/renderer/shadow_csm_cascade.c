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

/* The 8 corners of an AABB as vec3s. */
static void csm_box_corners(const float mn[3], const float mx[3], vec3_t out[8])
{
    for (int c = 0; c < 8; ++c)
        out[c] = (vec3_t){ (c&1)?mx[0]:mn[0], (c&2)?mx[1]:mn[1], (c&4)?mx[2]:mn[2] };
}

void shadow_csm_grid_dims(uint32_t n, float aspect,
                          uint32_t *cols, uint32_t *rows)
{
    /* Never divide by zero downstream: an empty/uninit CSM tiles as 1x1. */
    if (n == 0u) { if (cols) *cols = 1u; if (rows) *rows = 1u; return; }
    if (aspect <= 0.0f) aspect = 1.0f;
    /* Pick the factor pair (a x b == n) whose a/b is closest to the target
     * aspect, so tiles stay square-ish and texel density is even. Compared in
     * log space (ratio distance, symmetric for landscape vs portrait). A prime n
     * has only 1xn / nx1, so it collapses to a strip along the longer axis. */
    float target = logf(aspect);
    uint32_t best_a = 1u, best_b = n;
    float best_err = 1e30f;
    for (uint32_t a = 1u; a <= n; ++a) {
        if (n % a != 0u) continue;
        uint32_t b = n / a;
        float err = fabsf(logf((float)a / (float)b) - target);
        if (err < best_err) { best_err = err; best_a = a; best_b = b; }
    }
    if (cols) *cols = best_a;
    if (rows) *rows = best_b;
}

bool shadow_csm_caster_in_cascade(const shadow_csm_t *csm,
                                  const render_renderable_t *r,
                                  uint32_t cascade)
{
    if (csm == NULL || r == NULL || r->mesh == NULL || cascade >= csm->cascades)
        return false;
    /* Explicit per-caster tag wins: draw only into the tagged cascade. */
    if (r->shadow_cascade >= 0)
        return (uint32_t)r->shadow_cascade == cascade;
    /* Light-space XY box of the caster's world AABB (a caster and the shadow it
     * throws share this footprint in light space, so an XY overlap is enough). */
    float wmn[3], wmx[3];
    csm_world_aabb(r, wmn, wmx);
    vec3_t cor[8]; csm_box_corners(wmn, wmx, cor);
    float lx0 = 1e30f, ly0 = 1e30f, lx1 = -1e30f, ly1 = -1e30f;
    for (int j = 0; j < 8; ++j) {
        vec4_t p = mat4_mul_vec4(csm->light_view,
                                 (vec4_t){ cor[j].x, cor[j].y, cor[j].z, 1.0f });
        if (p.x < lx0) lx0 = p.x;
        if (p.x > lx1) lx1 = p.x;
        if (p.y < ly0) ly0 = p.y;
        if (p.y > ly1) ly1 = p.y;
    }
    /* Overlap the tile rect padded by a small filter guard (matches the fit). */
    const float g = 1.5f;
    float tx0 = csm->tile_min[cascade][0] - g, tx1 = csm->tile_max[cascade][0] + g;
    float ty0 = csm->tile_min[cascade][1] - g, ty1 = csm->tile_max[cascade][1] + g;
    return !(lx1 < tx0 || lx0 > tx1 || ly1 < ty0 || ly0 > ty1);
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

    /* --- Shared light-space basis for tiling + caster classification. The eye
     * is pulled back along -dir so the whole scene is in front (parallel proj:
     * this only offsets depth). Every tile and every caster is projected through
     * this ONE basis, so their light-space XY boxes are directly comparable. --- */
    vec3_t scene_center = { 0.5f*(smin[0]+smax[0]), 0.5f*(smin[1]+smax[1]),
                            0.5f*(smin[2]+smax[2]) };
    vec3_t leye = vec3_sub(scene_center, vec3_scale(dir, scene_diag + 1.0f));
    mat4_look_at(leye, scene_center, up, &csm->light_view);
    mat4_t linv; mat4_inverse(csm->light_view, &linv);   /* light-space -> world. */

    /* Scene light-space AABB: project the 8 world scene corners. */
    float Lmin[3] = { 1e30f, 1e30f, 1e30f }, Lmax[3] = { -1e30f, -1e30f, -1e30f };
    for (int j = 0; j < 8; ++j) {
        vec4_t p = mat4_mul_vec4(csm->light_view,
                                 (vec4_t){ scene_c[j].x, scene_c[j].y, scene_c[j].z, 1.0f });
        float v[3] = { p.x, p.y, p.z };
        for (int k = 0; k < 3; ++k) { if (v[k] < Lmin[k]) Lmin[k] = v[k];
                                      if (v[k] > Lmax[k]) Lmax[k] = v[k]; }
    }

    /* --- Grid the light-space XY extent into `cascades` tiles (closest to
     * square for even texel density), each cascade fit to ONE tile. --- */
    float Wx = Lmax[0] - Lmin[0], Wy = Lmax[1] - Lmin[1];
    if (Wx < 1e-4f) Wx = 1e-4f;
    if (Wy < 1e-4f) Wy = 1e-4f;
    uint32_t cols = 1u, rows = 1u;
    shadow_csm_grid_dims(csm->cascades, Wx / Wy, &cols, &rows);
    float tw = Wx / (float)cols, th = Wy / (float)rows;
    /* Filter guard: tiles overlap slightly so PCF taps and border casters have
     * data on both sides of a tile seam (light space, so metres). */
    float guard = 0.02f * ((tw < th) ? tw : th) + 1.0f;

    for (uint32_t c = 0; c < csm->cascades; ++c) {
        uint32_t col = c % cols, row = c / cols;
        float x0 = Lmin[0] + (float)col * tw, x1 = x0 + tw;
        float y0 = Lmin[1] + (float)row * th, y1 = y0 + th;
        /* Un-guarded rect drives caster classification (guard added there too). */
        csm->tile_min[c][0] = x0; csm->tile_min[c][1] = y0;
        csm->tile_max[c][0] = x1; csm->tile_max[c][1] = y1;
        /* World corners of the guard-expanded tile x FULL scene light-space depth
         * (every caster between light and receiver must render), then reuse the
         * ortho fit (near-plane extension + texel snap). */
        float gx0 = x0 - guard, gx1 = x1 + guard, gy0 = y0 - guard, gy1 = y1 + guard;
        vec3_t sc[8]; int n = 0;
        for (int zi = 0; zi < 2; ++zi) { float z = zi ? Lmax[2] : Lmin[2];
        for (int yi = 0; yi < 2; ++yi) { float y = yi ? gy1 : gy0;
        for (int xi = 0; xi < 2; ++xi) { float x = xi ? gx1 : gx0;
            vec4_t w = mat4_mul_vec4(linv, (vec4_t){ x, y, z, 1.0f });
            sc[n++] = (vec3_t){ w.x, w.y, w.z };
        } } }
        csm_fit_ortho(sc, dir, up, (float)csm->static_res, scene_c,
                      &csm->view_proj[c], csm->eye[c], &csm->far_plane[c],
                      &csm->texel_world[c]);
        if (getenv("CSM_DEBUG"))
            fprintf(stderr, "  tile %u: col%u row%u lx[%.1f,%.1f] ly[%.1f,%.1f] "
                    "texel=%.3fm far=%.1f\n", c, col, row, x0, x1, y0, y1,
                    csm->texel_world[c], csm->far_plane[c]);
    }

    /* --- Single dynamic map: fit to the whole scene so a dynamic caster
     * anywhere lands in it. --- */
    csm_fit_ortho(scene_c, dir, up, (float)csm->dynamic_res, scene_c,
                  &csm->dyn_view_proj, csm->dyn_eye, &csm->dyn_far, NULL);
}
