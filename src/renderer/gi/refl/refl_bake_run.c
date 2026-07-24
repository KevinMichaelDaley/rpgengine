/**
 * @file refl_bake_run.c
 * @brief Offline reflection-probe bake orchestrator (see refl_bake.h):
 *        placement over the scene AABB, per-probe cube render, octahedral
 *        resample, progressive prefilter, SDF specular-occlusion alpha,
 *        atlas assembly and the .rprobe write. One-shot offline pass --
 *        transient malloc here is deliberate (never per-frame).
 */
#include "ferrum/renderer/gi/refl_bake.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/probe/place/probe_chunk_sdf.h"
#include "ferrum/renderer/gi/refl_atlas.h"
#include "ferrum/renderer/gi/refl_file.h"
#include "ferrum/renderer/gi/refl_filter.h"
#include "ferrum/renderer/gi/refl_occl.h"
#include "ferrum/renderer/gi/refl_octa.h"
#include "ferrum/renderer/gi/refl_place.h"
#include "ferrum/renderer/mesh/static_mesh.h"

/* Column-major point transform (matches the renderer's model matrices). */
static void xform_point(const float m[16], const float p[3], float out[3])
{
    for (int r = 0; r < 3; ++r)
        out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
}

/* World AABB of every renderable (transformed mesh boxes). */
static bool scene_aabb(const render_scene_t *scene, float mn[3], float mx[3])
{
    bool any = false;
    for (int a = 0; a < 3; ++a) {
        mn[a] = 1e30f;
        mx[a] = -1e30f;
    }
    for (uint32_t i = 0; i < scene->count; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL)
            continue;
        for (uint32_t c = 0; c < 8u; ++c) {
            float lp[3] = {
                (c & 1u) ? r->mesh->aabb_max[0] : r->mesh->aabb_min[0],
                (c & 2u) ? r->mesh->aabb_max[1] : r->mesh->aabb_min[1],
                (c & 4u) ? r->mesh->aabb_max[2] : r->mesh->aabb_min[2],
            };
            float wp[3];
            xform_point(r->model, lp, wp);
            for (int a = 0; a < 3; ++a) {
                if (wp[a] < mn[a]) mn[a] = wp[a];
                if (wp[a] > mx[a]) mx[a] = wp[a];
            }
            any = true;
        }
    }
    return any;
}

/* Cone half-angle tangent for a mip's target roughness band. */
static float mip_cone_tan(uint32_t mip, uint32_t mips)
{
    float t = (mips > 1u) ? (float)mip / (float)(mips - 1u) : 0.0f;
    return 0.03f + (0.7f - 0.03f) * t * t;
}

bool refl_bake_run(const gl_loader_t *loader, const render_scene_t *scene,
                   const char *sdf_prefix, const refl_bake_params_t *prm_in)
{
    if (loader == NULL || scene == NULL || sdf_prefix == NULL ||
        prm_in == NULL)
        return false;
    refl_bake_params_t prm = *prm_in;
    if (prm.spacing <= 0.0f) prm.spacing = 12.0f;
    if (prm.tile_res < 8u) prm.tile_res = 32u;
    if (prm.face_res < 8u) prm.face_res = prm.tile_res;
    if (prm.mips == 0u || prm.mips > REFL_PROBE_MAX_MIPS) prm.mips = 4u;
    while ((prm.tile_res >> (prm.mips - 1u)) < 4u && prm.mips > 1u)
        prm.mips -= 1u;
    if (prm.max_probes == 0u) prm.max_probes = 1024u;
    if (prm.min_clear <= 0.0f) prm.min_clear = 0.75f;

    float mn[3], mx[3];
    if (!scene_aabb(scene, mn, mx))
        return false;

    probe_chunk_sdf_t cs;
    bool have_sdf = probe_chunk_sdf_open(sdf_prefix, &cs);

    refl_probe_t *probes = (refl_probe_t *)
        malloc((size_t)prm.max_probes * sizeof(refl_probe_t));
    if (probes == NULL) {
        if (have_sdf) probe_chunk_sdf_close(&cs);
        return false;
    }
    refl_probe_set_t set;
    refl_probe_set_init(&set, probes, prm.max_probes);
    refl_place_grid_fn(&set, mn, mx, prm.spacing,
                       have_sdf ? probe_chunk_sdf_sample : NULL,
                       have_sdf ? &cs : NULL, prm.min_clear,
                       have_sdf ? prm.spacing * 0.75f : 0.0f);
    if (set.count == 0u) {
        fprintf(stderr, "refl_bake: no clear probe positions (spacing %.1f)\n",
                (double)prm.spacing);
        free(probes);
        if (have_sdf) probe_chunk_sdf_close(&cs);
        return false;
    }
    set.tile_res = prm.tile_res;
    set.mips = prm.mips;
    set.tiles_x = 1u;
    while (set.tiles_x * set.tiles_x < set.count)
        set.tiles_x += 1u;
    set.tiles_y = (set.count + set.tiles_x - 1u) / set.tiles_x;

    /* Transient CPU buffers (offline): faces, tile work set, atlas mips. */
    bool ok = true;
    size_t face_n = (size_t)prm.face_res * prm.face_res * 4u;
    float *faces_mem = (float *)malloc(face_n * 6u * sizeof(float));
    size_t tile_n = (size_t)prm.tile_res * prm.tile_res * 4u;
    float *tile = (float *)malloc(tile_n * sizeof(float));
    float *tmp = (float *)malloc(tile_n * sizeof(float));
    float *down = (float *)malloc(tile_n / 4u * sizeof(float));
    float *mips[REFL_PROBE_MAX_MIPS] = { 0 };
    for (uint32_t m = 0; m < set.mips; ++m) {
        uint32_t w, h;
        refl_atlas_dims(&set, m, &w, &h);
        mips[m] = (float *)calloc((size_t)w * h * 4u, sizeof(float));
        ok = ok && mips[m] != NULL;
    }
    ok = ok && faces_mem != NULL && tile != NULL && tmp != NULL &&
         down != NULL;

    refl_bake_t rb;
    memset(&rb, 0, sizeof rb);
    ok = ok && refl_bake_init(&rb, loader, prm.face_res);

    for (uint32_t i = 0; ok && i < set.count; ++i) {
        refl_probe_t *pr = &set.probes[i];
        float *faces[6];
        for (uint32_t f = 0; f < 6u; ++f)
            faces[f] = faces_mem + (size_t)f * face_n;
        float sd[3] = { prm.sun_dir[0], prm.sun_dir[1], prm.sun_dir[2] };
        float sl = sqrtf(sd[0]*sd[0] + sd[1]*sd[1] + sd[2]*sd[2]);
        if (sl > 1e-6f) { sd[0] /= sl; sd[1] /= sl; sd[2] /= sl; }
        float sun_vis = have_sdf
            ? refl_occl_cone_fn(probe_chunk_sdf_sample, &cs, pr->pos, sd,
                                0.08f, 120.0f)
            : 1.0f;
        refl_bake_probe(&rb, scene, pr->pos, &prm, sun_vis, faces);
        const float *cfaces[6] = { faces[0], faces[1], faces[2],
                                   faces[3], faces[4], faces[5] };
        refl_octa_from_cube(cfaces, prm.face_res, tile, prm.tile_res);

        uint32_t res = prm.tile_res;
        float ao_sum = 0.0f;
        uint32_t ao_n = 0u;
        for (uint32_t m = 0; m < set.mips; ++m) {
            /* Progressive: each level smooths, then halves into the next. */
            if (m > 0u) {
                float sharp = 24.0f / (float)(1u << m);
                refl_filter_smooth(tile, res, sharp, tmp);
                refl_filter_downsample(tile, res, down);
                res /= 2u;
                memcpy(tile, down, (size_t)res * res * 4u * sizeof(float));
            }
            /* Specular-occlusion alpha: cone-march the baked SDF per texel
             * direction; the LAST mip's mean becomes the probe's ao. */
            float ctan = mip_cone_tan(m, set.mips);
            for (uint32_t y = 0; y < res; ++y)
                for (uint32_t x = 0; x < res; ++x) {
                    float uv[2] = { ((float)x + 0.5f) / (float)res,
                                    ((float)y + 0.5f) / (float)res };
                    float d[3];
                    refl_octa_decode(uv, d);
                    float a = have_sdf
                        ? refl_occl_cone_fn(probe_chunk_sdf_sample, &cs,
                                            pr->pos, d, ctan,
                                            prm.spacing * 2.0f)
                        : 1.0f;
                    tile[((size_t)y * res + x) * 4u + 3u] = a;
                    if (m + 1u == set.mips) {
                        ao_sum += a;
                        ao_n += 1u;
                    }
                }
            /* Blit the tile into its atlas rect at this mip. */
            uint32_t tx, ty, tr;
            if (refl_atlas_tile_rect(&set, pr->tile, m, &tx, &ty, &tr)) {
                uint32_t aw, ah;
                refl_atlas_dims(&set, m, &aw, &ah);
                (void)ah;
                for (uint32_t y = 0; y < tr; ++y)
                    memcpy(&mips[m][((size_t)(ty + y) * aw + tx) * 4u],
                           &tile[(size_t)y * res * 4u],
                           (size_t)tr * 4u * sizeof(float));
            }
        }
        pr->ao = (ao_n > 0u) ? ao_sum / (float)ao_n : 1.0f;
    }
    refl_bake_destroy(&rb);

    if (ok) {
        char path[576];
        snprintf(path, sizeof path, "%s.rprobe", sdf_prefix);
        const float *cmips[REFL_PROBE_MAX_MIPS] = { 0 };
        for (uint32_t m = 0; m < set.mips; ++m)
            cmips[m] = mips[m];
        ok = refl_file_save(path, &set, cmips);
        if (ok)
            fprintf(stderr,
                    "refl_bake: %u probes, tile %u, %u mips -> %s\n",
                    set.count, set.tile_res, set.mips, path);
        else
            fprintf(stderr, "refl_bake: FAILED writing %s\n", path);
    }

    for (uint32_t m = 0; m < REFL_PROBE_MAX_MIPS; ++m)
        free(mips[m]);
    free(down);
    free(tmp);
    free(tile);
    free(faces_mem);
    free(probes);
    if (have_sdf)
        probe_chunk_sdf_close(&cs);
    return ok;
}
