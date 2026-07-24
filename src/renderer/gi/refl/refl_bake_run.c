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
    if (loader == NULL || sdf_prefix == NULL || prm_in == NULL)
        return false;
    if (scene == NULL &&
        (prm_in->render_fn == NULL || prm_in->place_min == NULL))
        return false;   /* no scene needs both a renderer and a box. */
    refl_bake_params_t prm = *prm_in;
    if (prm.spacing <= 0.0f) prm.spacing = 12.0f;
    if (prm.tile_res < 8u) prm.tile_res = 32u;
    if (prm.face_res < 8u) prm.face_res = prm.tile_res * 2u;  /* supersample */
    if (prm.mips == 0u || prm.mips > REFL_PROBE_MAX_MIPS) prm.mips = 4u;
    while ((prm.tile_res >> (prm.mips - 1u)) < 4u && prm.mips > 1u)
        prm.mips -= 1u;
    if (prm.max_probes == 0u) prm.max_probes = 1024u;
    {
        const char *emp = getenv("REFL_MAXP");
        if (emp != NULL && atoi(emp) > 0)
            prm.max_probes = (uint32_t)atoi(emp);
    }
    if (prm.min_clear <= 0.0f) prm.min_clear = 0.75f;
    if (prm.depth_res == 0u) prm.depth_res = 16u;

    float mn[3], mx[3];
    if (prm.place_min != NULL && prm.place_max != NULL) {
        for (int a2 = 0; a2 < 3; ++a2) {
            mn[a2] = prm.place_min[a2];
            mx[a2] = prm.place_max[a2];
        }
    } else if (!scene_aabb(scene, mn, mx)) {
        return false;
    }

    probe_chunk_sdf_t cs;
    memset(&cs, 0, sizeof cs);
    bool own_sdf = false;
    refl_sdf_fn sdf_fn = prm.sdf_fn;
    void *sdf_user = prm.sdf_user;
    if (sdf_fn == NULL) {
        own_sdf = probe_chunk_sdf_open(sdf_prefix, &cs);
        if (own_sdf) {
            sdf_fn = probe_chunk_sdf_sample;
            sdf_user = &cs;
        }
    }
    bool have_sdf = sdf_fn != NULL;

    refl_probe_t *probes = (refl_probe_t *)
        malloc((size_t)prm.max_probes * sizeof(refl_probe_t));
    if (probes == NULL) {
        if (own_sdf) probe_chunk_sdf_close(&cs);
        return false;
    }
    refl_probe_set_t set;
    refl_probe_set_init(&set, probes, prm.max_probes);
    refl_place_grid_fn(&set, mn, mx, prm.spacing, sdf_fn, sdf_user,
                       prm.min_clear,
                       have_sdf ? prm.spacing * 0.75f : 0.0f);
    if (set.count == 0u) {
        if (prm.out_path == NULL)
            fprintf(stderr,
                    "refl_bake: no clear probe positions (spacing %.1f)\n",
                    (double)prm.spacing);
        free(probes);
        if (own_sdf) probe_chunk_sdf_close(&cs);
        return false;
    }
    set.tile_res = prm.tile_res;
    set.mips = prm.mips;
    set.depth_res = prm.depth_res;
    set.tiles_x = 1u;
    while (set.tiles_x * set.tiles_x < set.count)
        set.tiles_x += 1u;
    set.tiles_y = (set.count + set.tiles_x - 1u) / set.tiles_x;

    /* Transient CPU buffers (offline): faces, tile work set, atlas mips,
     * plus the raw-depth faces + RG visibility-depth atlas. */
    bool ok = true;
    size_t face_n = (size_t)prm.face_res * prm.face_res * 4u;
    float *faces_mem = (float *)malloc(face_n * 6u * sizeof(float));
    size_t dface_n = (size_t)prm.face_res * prm.face_res;
    float *dfaces_mem = (float *)malloc(dface_n * 6u * sizeof(float));
    float *dpack = (float *)malloc(dface_n * 6u * 4u * sizeof(float));
    size_t dtile_n = (size_t)prm.depth_res * prm.depth_res * 4u;
    float *dtile = (float *)malloc(dtile_n * sizeof(float));
    float *dtmp = (float *)malloc(dtile_n * sizeof(float));
    uint32_t datlas_w = set.tiles_x * prm.depth_res;
    uint32_t datlas_h = set.tiles_y * prm.depth_res;
    float *datlas = (float *)calloc((size_t)datlas_w * datlas_h * 2u,
                                    sizeof(float));
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
         down != NULL && dfaces_mem != NULL && dpack != NULL &&
         dtile != NULL && dtmp != NULL && datlas != NULL;

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
            ? refl_occl_cone_fn(sdf_fn, sdf_user, pr->pos, sd, 0.08f,
                                120.0f)
            : 1.0f;
        /* Sky openness at the probe: enclosed interiors must not bake the
         * full outdoor ambient (that made every dungeon reflect like wet
         * chrome). Cheap 16-direction upper-hemisphere cone estimate. */
        float open_sum = 0.0f;
        if (have_sdf) {
            for (int oz = 0; oz < 4; ++oz)
                for (int ox = 0; ox < 4; ++ox) {
                    float uv2[2] = { ((float)ox + 0.5f) / 4.0f,
                                     ((float)oz + 0.5f) / 4.0f };
                    float d2[3];
                    refl_octa_decode(uv2, d2);
                    if (d2[1] < 0.0f)
                        d2[1] = -d2[1];   /* fold into +y (up). */
                    open_sum += refl_occl_cone_fn(sdf_fn, sdf_user,
                                                  pr->pos, d2, 0.45f,
                                                  prm.spacing * 4.0f);
                }
        } else {
            open_sum = 16.0f;
        }
        float openness = open_sum / 16.0f;
        refl_bake_params_t pprm = prm;
        for (int a2 = 0; a2 < 3; ++a2) {
            float amb_scale = 0.15f + 0.85f * openness;
            pprm.ambient[a2] = prm.ambient[a2] * amb_scale;
            pprm.sky[a2] = prm.sky[a2] * amb_scale;
        }
        float *dfaces[6];
        for (uint32_t f = 0; f < 6u; ++f)
            dfaces[f] = dfaces_mem + (size_t)f * dface_n;
        refl_bake_probe(&rb, scene, pr->pos, &pprm, sun_vis, faces,
                        dfaces);
        /* REFL_DUMP=1: write probe 0's six RAW faces (pre-octa, pre-gamma
         * inversion) as PPMs -- the ground truth of what the pipeline
         * callback actually rendered into the bake FBO. */
        if ((i == 0u || i == set.count / 2u) &&
            getenv("REFL_DUMP") != NULL) {
            for (uint32_t f = 0; f < 6u; ++f) {
                char dp[64];
                snprintf(dp, sizeof dp, "build/refl_p%u_face%u.ppm", i, f);
                FILE *df = fopen(dp, "wb");
                if (df != NULL) {
                    fprintf(df, "P6\n%u %u\n255\n", prm.face_res,
                            prm.face_res);
                    for (size_t t = 0; t < face_n; t += 4u) {
                        for (int ch = 0; ch < 3; ++ch) {
                            float v = faces[f][t + (size_t)ch];
                            if (v < 0.0f) v = 0.0f;
                            if (v > 1.0f) v = 1.0f;
                            fputc((int)(v * 255.0f), df);
                        }
                    }
                    fclose(df);
                }
            }
            fprintf(stderr, "refl_bake: dumped probe %u at "
                    "(%.1f, %.1f, %.1f)\n", i, (double)pr->pos[0],
                    (double)pr->pos[1], (double)pr->pos[2]);
        }
        /* Full-pipeline output is gamma-encoded for display: invert it so
         * the atlas stores LINEAR radiance (re-gammaed at shade time). */
        if (prm.render_fn != NULL)
            for (uint32_t f = 0; f < 6u; ++f)
                for (size_t t = 0; t < face_n; t += 4u) {
                    faces[f][t + 0u] = powf(faces[f][t + 0u], 2.2f);
                    faces[f][t + 1u] = powf(faces[f][t + 1u], 2.2f);
                    faces[f][t + 2u] = powf(faces[f][t + 2u], 2.2f);
                }
        const float *cfaces[6] = { faces[0], faces[1], faces[2],
                                   faces[3], faces[4], faces[5] };
        refl_octa_from_cube(cfaces, prm.face_res, tile, prm.tile_res);

        /* Visibility depth (DDGI-style): raw hardware depth -> RADIAL
         * linear distance per face texel (clamped to 2x the influence
         * range so open sky cannot blow the Chebyshev variance), packed as
         * (r, r^2), octa-resampled to the depth tile, one smoothing pass,
         * then blitted into the RG atlas. */
        {
            const float zn = 0.05f, zf = 500.0f;
            float dclamp = prm.spacing * 2.0f;
            for (uint32_t f = 0; f < 6u; ++f)
                for (uint32_t y = 0; y < prm.face_res; ++y)
                    for (uint32_t x = 0; x < prm.face_res; ++x) {
                        float z = dfaces[f][(size_t)y * prm.face_res + x];
                        float lin = zn * zf / (zf - z * (zf - zn));
                        float sc = 2.0f * (((float)x + 0.5f) /
                                           (float)prm.face_res) - 1.0f;
                        float tc = 2.0f * (((float)y + 0.5f) /
                                           (float)prm.face_res) - 1.0f;
                        float rad = lin * sqrtf(1.0f + sc*sc + tc*tc);
                        if (rad > dclamp)
                            rad = dclamp;
                        float *o = &dpack[((size_t)f * dface_n +
                                           (size_t)y * prm.face_res + x) *
                                          4u];
                        o[0] = rad;
                        o[1] = rad * rad;
                        o[2] = 0.0f;
                        o[3] = 0.0f;
                    }
            const float *cdf[6] = {
                dpack + 0u * dface_n * 4u, dpack + 1u * dface_n * 4u,
                dpack + 2u * dface_n * 4u, dpack + 3u * dface_n * 4u,
                dpack + 4u * dface_n * 4u, dpack + 5u * dface_n * 4u,
            };
            refl_octa_from_cube(cdf, prm.face_res, dtile, prm.depth_res);
            refl_filter_smooth(dtile, prm.depth_res, 32.0f, dtmp);
            uint32_t dx0 = (pr->tile % set.tiles_x) * prm.depth_res;
            uint32_t dy0 = (pr->tile / set.tiles_x) * prm.depth_res;
            for (uint32_t y = 0; y < prm.depth_res; ++y)
                for (uint32_t x = 0; x < prm.depth_res; ++x) {
                    float *o = &datlas[((size_t)(dy0 + y) * datlas_w +
                                        (dx0 + x)) * 2u];
                    const float *i2 =
                        &dtile[((size_t)y * prm.depth_res + x) * 4u];
                    o[0] = i2[0];
                    o[1] = i2[1];
                }
        }

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
            /* Specular-occlusion alpha: cone-march the baked SDF on a
             * FIXED 32x32 octa grid (occlusion is low-frequency; marching
             * per atlas texel at mirror-grade tile sizes would explode),
             * then bilinearly lift it onto this mip's texels. The LAST
             * mip's mean becomes the probe's ao. */
            enum { OCC_RES = 32 };
            static float occ[OCC_RES * OCC_RES];
            float ctan = mip_cone_tan(m, set.mips);
            for (uint32_t y = 0; y < OCC_RES; ++y)
                for (uint32_t x = 0; x < OCC_RES; ++x) {
                    float uv[2] = { ((float)x + 0.5f) / (float)OCC_RES,
                                    ((float)y + 0.5f) / (float)OCC_RES };
                    float d[3];
                    refl_octa_decode(uv, d);
                    occ[y * OCC_RES + x] = have_sdf
                        ? refl_occl_cone_fn(sdf_fn, sdf_user, pr->pos, d,
                                            ctan, prm.spacing * 2.0f)
                        : 1.0f;
                }
            for (uint32_t y = 0; y < res; ++y)
                for (uint32_t x = 0; x < res; ++x) {
                    float fx = (((float)x + 0.5f) / (float)res) * OCC_RES
                               - 0.5f;
                    float fy = (((float)y + 0.5f) / (float)res) * OCC_RES
                               - 0.5f;
                    int32_t x0 = (int32_t)floorf(fx);
                    int32_t y0 = (int32_t)floorf(fy);
                    float wx = fx - (float)x0, wy = fy - (float)y0;
                    float a = 0.0f;
                    for (int32_t j = 0; j < 2; ++j)
                        for (int32_t i2 = 0; i2 < 2; ++i2) {
                            int32_t sx = x0 + i2, sy = y0 + j;
                            if (sx < 0) sx = 0;
                            if (sy < 0) sy = 0;
                            if (sx >= OCC_RES) sx = OCC_RES - 1;
                            if (sy >= OCC_RES) sy = OCC_RES - 1;
                            a += occ[sy * OCC_RES + sx] *
                                 (i2 ? wx : 1.0f - wx) *
                                 (j ? wy : 1.0f - wy);
                        }
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
        if (prm.out_path != NULL)
            snprintf(path, sizeof path, "%s", prm.out_path);
        else
            snprintf(path, sizeof path, "%s.rprobe", sdf_prefix);
        const float *cmips[REFL_PROBE_MAX_MIPS] = { 0 };
        for (uint32_t m = 0; m < set.mips; ++m)
            cmips[m] = mips[m];
        ok = refl_file_save(path, &set, cmips, datlas);
        if (ok)
            fprintf(stderr,
                    "refl_bake: %u probes, tile %u, %u mips -> %s\n",
                    set.count, set.tile_res, set.mips, path);
        else
            fprintf(stderr, "refl_bake: FAILED writing %s\n", path);
    }

    for (uint32_t m = 0; m < REFL_PROBE_MAX_MIPS; ++m)
        free(mips[m]);
    free(datlas);
    free(dtmp);
    free(dtile);
    free(dpack);
    free(dfaces_mem);
    free(down);
    free(tmp);
    free(tile);
    free(faces_mem);
    free(probes);
    if (own_sdf)
        probe_chunk_sdf_close(&cs);
    return ok;
}
