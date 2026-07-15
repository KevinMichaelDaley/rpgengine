/**
 * @file lm_mesh_bake.c
 * @brief Triangle-mesh lightmap bake orchestrator (see lm_mesh_bake.h).
 */
#include "ferrum/lightmap/lm_mesh_bake.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/lightmap/gpu/lm_gpu_gather.h"
#include "ferrum/lightmap/lm_gi_gather.h"
#include "ferrum/lightmap/lm_svo_mip.h"
#include "ferrum/lightmap/lm_svo_voxelize.h"
#include "ferrum/lightmap/lm_mesh_luxel.h"
#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_svo_material.h"
#include "ferrum/lightmap/lm_visibility.h"
#include "ferrum/physics/mesh_collider.h"

#define LM_MESH_DEFAULT_RES 32u

/* Effective atlas resolution for a mesh (falls back to a default). */
static uint32_t lm_mesh_res(const lm_mesh_t *m)
{
    return (m->lightmap_resolution > 0u) ? m->lightmap_resolution
                                         : LM_MESH_DEFAULT_RES;
}

/* Sum of triangle areas of a mesh (world space). */
static float lm_mesh_area(const lm_mesh_t *m)
{
    float area = 0.0f;
    for (uint32_t t = 0; t + 2 < m->index_count; t += 3) {
        uint32_t a = m->indices[t], b = m->indices[t + 1], c = m->indices[t + 2];
        vec3_t p0 = { m->positions[a*3], m->positions[a*3+1], m->positions[a*3+2] };
        vec3_t p1 = { m->positions[b*3], m->positions[b*3+1], m->positions[b*3+2] };
        vec3_t p2 = { m->positions[c*3], m->positions[c*3+1], m->positions[c*3+2] };
        vec3_t cr = vec3_cross(vec3_sub(p1, p0), vec3_sub(p2, p0));
        area += 0.5f * vec3_magnitude(cr);
    }
    return area;
}

/* Stamp a mesh's triangles into the SVO with its material id. */
static void lm_mesh_stamp(npc_svo_grid_t *svo, const lm_mesh_t *m)
{
    for (uint32_t t = 0; t + 2 < m->index_count; t += 3) {
        uint32_t a = m->indices[t], b = m->indices[t + 1], c = m->indices[t + 2];
        phys_triangle_t tri;
        tri.v[0] = (phys_vec3_t){ m->positions[a*3], m->positions[a*3+1], m->positions[a*3+2] };
        tri.v[1] = (phys_vec3_t){ m->positions[b*3], m->positions[b*3+1], m->positions[b*3+2] };
        tri.v[2] = (phys_vec3_t){ m->positions[c*3], m->positions[c*3+1], m->positions[c*3+2] };
        lm_svo_stamp_triangle(svo, &tri, m->material);
    }
}

/* Bake the analytic lights' DIRECT illumination into each luxel's SH: value =
 * incident irradiance, weight = 1, so the SH cosine-lobe reconstruction yields
 * irradiance*max(N.L,0). SVO-shadowed with a normal shadow bias. */
static void lm_mesh_bake_direct(lm_lightmap_t *lm, const lm_light_t *lights,
                                uint32_t n_lights, const npc_svo_grid_t *svo)
{
    float eps = svo ? svo->voxel_size * 1.5f : 0.0f;
    uint32_t n = lm->res_u * lm->res_v;
    for (uint32_t i = 0; i < n; ++i) {
        lm_luxel_t *lx = &lm->luxels[i];
        for (uint32_t j = 0; j < n_lights; ++j) {
            vec3_t dir, irr; float dist;
            if (!lm_light_incident(&lights[j], lx->pos, &dir, &dist, &irr))
                continue;
            if (vec3_dot(dir, lx->normal) <= 0.0f)
                continue;
            if (svo) {
                vec3_t o = vec3_add(lx->pos, vec3_scale(lx->normal, eps));
                if (lm_visibility_occluded(svo, o, dir, dist))
                    continue;
            }
            const float *ic = &irr.x;
            for (int c = 0; c < 3; ++c)
                lm_sh9_add_sample(&lx->sh[c], dir, ic[c], 1.0f);
        }
    }
}

static float lm_mesh_rngf(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return (float)(*s >> 8) * (1.0f / 16777216.0f);
}

/* Direct illumination from EMISSIVE meshes treated as area lights: stratified
 * triangle-area sampling, SVO-shadowed, deposited into each luxel's SH (the
 * mesh analogue of lm_direct for quads). */
static void lm_mesh_bake_emissive(lm_lightmap_t *lm, const lm_mesh_scene_t *scene,
                                  const npc_svo_grid_t *svo, uint32_t samples,
                                  uint32_t seed)
{
    uint32_t ns = (uint32_t)ceilf(sqrtf((float)(samples ? samples : 1)));
    if (ns == 0) ns = 1;
    float eps = svo ? svo->voxel_size * 1.5f : 0.0f;
    uint32_t nlx = lm->res_u * lm->res_v;
    for (uint32_t li = 0; li < nlx; ++li) {
        lm_luxel_t *lx = &lm->luxels[li];
        uint32_t rng = seed ^ (li * 2654435761u);
        for (uint32_t mi = 0; mi < scene->n_meshes; ++mi) {
            const lm_mesh_t *me = &scene->meshes[mi];
            int emissive = me->emissive_image != NULL || me->emissive.x > 0.0f ||
                           me->emissive.y > 0.0f || me->emissive.z > 0.0f;
            if (!emissive)
                continue;
            for (uint32_t t = 0; t + 2 < me->index_count; t += 3) {
                uint32_t a = me->indices[t], b = me->indices[t+1], c = me->indices[t+2];
                vec3_t A = { me->positions[a*3], me->positions[a*3+1], me->positions[a*3+2] };
                vec3_t B = { me->positions[b*3], me->positions[b*3+1], me->positions[b*3+2] };
                vec3_t C = { me->positions[c*3], me->positions[c*3+1], me->positions[c*3+2] };
                vec3_t cr = vec3_cross(vec3_sub(B, A), vec3_sub(C, A));
                float mag = vec3_magnitude(cr);
                if (mag < 1e-9f) continue;
                float area = 0.5f * mag;
                vec3_t tn = vec3_scale(cr, 1.0f / mag);
                uint32_t total = ns * ns;
                for (uint32_t s = 0; s < total; ++s) {
                    float r1 = lm_mesh_rngf(&rng), r2 = lm_mesh_rngf(&rng);
                    float su = sqrtf(r1);
                    vec3_t p = vec3_add(vec3_add(vec3_scale(A, 1.0f - su),
                                                 vec3_scale(B, su * (1.0f - r2))),
                                        vec3_scale(C, su * r2));
                    vec3_t d = vec3_sub(p, lx->pos);
                    float r2d = vec3_dot(d, d);
                    if (r2d < 1e-8f) continue;
                    vec3_t dir = vec3_scale(d, 1.0f / sqrtf(r2d));
                    if (vec3_dot(dir, lx->normal) <= 0.0f) continue;
                    float cose = vec3_dot(vec3_scale(dir, -1.0f), tn);
                    if (cose <= 0.0f) continue;
                    if (svo) {
                        vec3_t o = vec3_add(lx->pos, vec3_scale(lx->normal, eps));
                        vec3_t pe = vec3_add(p, vec3_scale(tn, eps));
                        if (!lm_visibility_segment(svo, o, pe)) continue;
                    }
                    /* Emissive radiance from the material's emissive texture at
                     * the sampled point (barycentric uv0), times the tint. */
                    vec3_t es = me->emissive;
                    if (me->uv0 && me->emissive_image) {
                        float w0 = 1.0f - su, w1 = su * (1.0f - r2), w2 = su * r2;
                        float eu = w0*me->uv0[a*2]   + w1*me->uv0[b*2]   + w2*me->uv0[c*2];
                        float ev = w0*me->uv0[a*2+1] + w1*me->uv0[b*2+1] + w2*me->uv0[c*2+1];
                        vec3_t sv = lm_image_sample(me->emissive_image, eu, ev);
                        es = (vec3_t){ sv.x*me->emissive.x, sv.y*me->emissive.y, sv.z*me->emissive.z };
                    }
                    float w = cose * area / (float)total / r2d;
                    const float *ec = &es.x;
                    for (int ch = 0; ch < 3; ++ch)
                        lm_sh9_add_sample(&lx->sh[ch], dir, ec[ch], w);
                }
            }
        }
    }
}

bool lm_mesh_bake(const lm_mesh_scene_t *scene, const lm_bake_config_t *config,
                  lm_mesh_bake_result_t *result, arena_t *arena)
{
    uint32_t nm = scene->n_meshes;

    /* Atlas rects: one per mesh, sized by its lightmap resolution. */
    lm_atlas_rect_t *rects =
        arena_alloc(arena, _Alignof(lm_atlas_rect_t), (nm ? nm : 1) * sizeof(lm_atlas_rect_t));
    if (!rects)
        return false;
    uint32_t cap = 0u, max_area = 1u;
    for (uint32_t i = 0; i < nm; ++i) {
        uint32_t r = lm_mesh_res(&scene->meshes[i]);
        rects[i] = (lm_atlas_rect_t){ r, r, 0, 0 };
        cap += r * r;
        if (r * r > max_area) max_area = r * r;
    }
    if (!lm_atlas_pack(rects, nm, config->atlas_width, config->atlas_padding,
                       &result->atlas))
        return false;
    result->rects = rects;
    result->n_meshes = nm;

    /* Luxel storage (dense capacity = sum of rect areas). */
    cap = cap ? cap : 1u;
    result->combined.luxels = arena_alloc(arena, _Alignof(lm_luxel_t), cap * sizeof(lm_luxel_t));
    result->atlas_x = arena_alloc(arena, _Alignof(uint32_t), cap * sizeof(uint32_t));
    result->atlas_y = arena_alloc(arena, _Alignof(uint32_t), cap * sizeof(uint32_t));
    result->luxel_areas = arena_alloc(arena, _Alignof(float), cap * sizeof(float));
    vec3_t *positions = arena_alloc(arena, _Alignof(vec3_t), cap * sizeof(vec3_t));
    uint8_t *visited = arena_alloc(arena, 1, max_area);
    if (!result->combined.luxels || !result->atlas_x || !result->atlas_y ||
        !result->luxel_areas || !positions || !visited)
        return false;

    /* SVO for visibility + far field. Derive the depth from the target voxel
     * size when set (voxel edge = max_extent / 2^depth), so callers specify a
     * physical resolution (default ~1cm) rather than a raw depth. */
    uint32_t svo_depth = config->svo_depth;
    if (config->voxel_size > 0.0f) {
        phys_vec3_t mn = config->svo_bounds.min, mx = config->svo_bounds.max;
        float ext = mx.x - mn.x;
        if (mx.y - mn.y > ext) ext = mx.y - mn.y;
        if (mx.z - mn.z > ext) ext = mx.z - mn.z;
        /* Pick the depth whose voxel edge is CLOSEST to the requested size (round
         * to nearest), not the first one at or below it -- the latter overshoots
         * by up to 2x (e.g. 4cm requested -> 2.3cm), needlessly doubling voxel
         * count in every axis. */
        uint32_t d = 1u;
        while (d < NPC_SVO_MAX_DEPTH &&
               ext / (float)(1u << d) > config->voxel_size)
            ++d;
        if (d > 1u) {
            float coarse = ext / (float)(1u << (d - 1u)); /* > requested */
            float fine = ext / (float)(1u << d);          /* <= requested */
            if (coarse - config->voxel_size < config->voxel_size - fine)
                --d; /* the coarser (bigger) voxel is closer to the target */
        }
        svo_depth = d;
    }
    /* The chunked GPU path (rpg-fzht) builds a fine SVO PER CHUNK inside the
     * gather, so it must NOT build (or voxelize) one whole-scene octree -- that
     * node count, and its node-count-sized voxelize scratch, is exactly what
     * blows up on a massive scene. Every other path uses the whole-scene SVO. */
    bool chunked_gpu = config->gpu_gather && config->chunk_size > 0.0f;
    npc_svo_grid_t svo; memset(&svo, 0, sizeof svo);
    if (!chunked_gpu && !npc_svo_grid_init(&svo, config->svo_bounds, svo_depth))
        return false;

    /* Luxelize each mesh + record areas + stamp the SVO. */
    uint32_t total = 0u;
    for (uint32_t i = 0; i < nm; ++i) {
        const lm_mesh_t *m = &scene->meshes[i];
        uint32_t c = lm_mesh_luxelize(m, &rects[i], result->atlas.width,
                                      result->atlas.height,
                                      &result->combined.luxels[total],
                                      &result->atlas_x[total],
                                      &result->atlas_y[total], visited);
        float area = lm_mesh_area(m);
        float per = (c > 0) ? area / (float)c : 0.0f;
        for (uint32_t k = 0; k < c; ++k)
            result->luxel_areas[total + k] = per;
        total += c;
        if (!chunked_gpu) lm_mesh_stamp(&svo, m);
    }
    result->combined.res_u = total;
    result->combined.res_v = 1;
    result->n_luxels = total;
    for (uint32_t i = 0; i < total; ++i)
        positions[i] = result->combined.luxels[i].pos;

    if (total > 0) {
        /* The GPU gather path bakes INDIRECT ONLY: analytic-light direct (the sun)
         * is provided at runtime by the realtime CSM, and area lights (emissive)
         * are captured by the GPU gather itself (emissive voxels hit while
         * tracing). Baking the sun's direct on the CPU here is both redundant with
         * the realtime sun AND non-portable -- lm_visibility's DDA decides the
         * shadow boundary at the ULP level, so it splotches differently per CPU
         * (e.g. the chimera bake box). Only the legacy CPU path bakes direct. */
        if (!config->gpu_gather) {
            /* Direct sun/emissive onto the luxels' own SH (their direct term). */
            lm_mesh_bake_direct(&result->combined, scene->lights, scene->n_lights, &svo);
            lm_mesh_bake_emissive(&result->combined, scene, &svo,
                                  config->direct_samples ? config->direct_samples : 64u,
                                  config->seed ^ 0x51EDu);
        }
        if (config->farfield_samples > 0) {
            /* Voxelize the surfaces' textured material into the SVO, then do the
             * unified path-traced GI gather: near hits path-trace the voxel
             * material (direct + cosine bounce), rays past the transition cone
             * the octree, escapes read the sky. Replaces the old near-field
             * radiosity solve + discard-near far-field gather. */
            /* area/vnormal are node-count sized -- only the whole-scene SVO paths
             * (CPU + non-chunked GPU) allocate + voxelize them. The chunked GPU
             * path builds + voxelizes a fine SVO PER CHUNK inside the gather. */
            float  *area    = chunked_gpu ? NULL : arena_alloc(arena, _Alignof(float),
                                      (size_t)svo.node_count * sizeof(float));
            vec3_t *vnormal = chunked_gpu ? NULL : arena_alloc(arena, _Alignof(vec3_t),
                                          (size_t)svo.node_count * sizeof(vec3_t));
            /* Progressive gather: the batch accumulator @c accum (3 SH sets per
             * luxel) sums independent small batches; @c de holds the luxels'
             * direct/emissive SH so each preview rebases onto it. The running
             * lightmap after batch b is de + accum/(b+1). Never does one huge
             * per-luxel gather -- reaches farfield_samples by averaging batches. */
            lm_sh9_t *accum = arena_alloc(arena, _Alignof(lm_sh9_t),
                                          (size_t)total * 3u * sizeof(lm_sh9_t));
            lm_sh9_t *de = arena_alloc(arena, _Alignof(lm_sh9_t),
                                       (size_t)total * 3u * sizeof(lm_sh9_t));
            if (accum && de && (chunked_gpu || (area && vnormal))) {
                if (!chunked_gpu)
                    lm_svo_voxelize(&svo, scene->meshes, scene->n_meshes, area, vnormal);
                for (uint32_t i = 0; i < total; ++i)
                    for (int c = 0; c < 3; ++c) {
                        de[i * 3 + c] = result->combined.luxels[i].sh[c];
                        lm_sh9_zero(&accum[i * 3 + c]);
                    }
                uint32_t batch = config->gi_batch ? config->gi_batch : 64u;
                uint32_t nb = (config->farfield_samples + batch - 1u) / batch;
                /* GPU path (rpg-k4lk): one gather does all samples; skip the CPU
                 * batch loop. chunk_size > 0 -> chunked (rpg-fzht): a fine SVO +
                 * near/medium/far SDF hierarchy PER CHUNK over the scene bounds,
                 * so no whole-scene octree/field is ever built. On GPU failure the
                 * non-chunked path falls back to the CPU gather; the chunked path
                 * cannot (no whole-scene SVO) and reports the failure. */
                bool gpu_ok = false;
                if (config->gpu_gather) {
                    gpu_ok = config->chunk_size > 0.0f
                       ? lm_gpu_gather_chunked(&result->combined, accum,
                                      config->svo_bounds, config->voxel_size, scene,
                                      config->chunk_size, config->chunk_margin, scene->lights,
                                      scene->n_lights, &config->sky,
                                      config->farfield_near, config->farfield_maxdist,
                                      config->farfield_samples, config->gi_bounces,
                                      config->seed ^ 0x9E3779B9u)
                       : lm_gpu_gather_run(&result->combined, accum, &svo, scene, NULL, NULL,
                                      scene->lights, scene->n_lights, &config->sky,
                                      config->farfield_near, config->farfield_maxdist,
                                      config->farfield_samples, config->gi_bounces,
                                      config->seed ^ 0x9E3779B9u);
                }
                if (gpu_ok) {
                    for (uint32_t i = 0; i < total; ++i)
                        for (int c = 0; c < 3; ++c)
                            for (int k = 0; k < 9; ++k)
                                result->combined.luxels[i].sh[c].c[k] =
                                    de[i * 3 + c].c[k] + accum[i * 3 + c].c[k];
                    if (config->on_batch) config->on_batch(config->on_batch_ud, nb, nb);
                    nb = 0; /* skip the CPU loop below. */
                } else if (chunked_gpu) {
                    nb = 0; /* no whole-scene SVO -> no CPU fallback available. */
                }
                for (uint32_t b = 0; b < nb; ++b) {
                    lm_gi_gather(&result->combined, accum, &svo, scene->lights,
                                 scene->n_lights, &config->sky, vnormal,
                                 config->farfield_near, config->farfield_maxdist,
                                 batch, config->gi_bounces,
                                 config->seed ^ 0x9E3779B9u ^ (b * 0x85EBCA6Bu),
                                 config->gi_threads);
                    /* Fold the running mean (accum / batches so far) onto the
                     * direct/emissive base into the luxel SH. */
                    float inv = 1.0f / (float)(b + 1u);
                    for (uint32_t i = 0; i < total; ++i)
                        for (int c = 0; c < 3; ++c)
                            for (int k = 0; k < 9; ++k)
                                result->combined.luxels[i].sh[c].c[k] =
                                    de[i * 3 + c].c[k] + accum[i * 3 + c].c[k] * inv;
                    if (config->on_batch)
                        config->on_batch(config->on_batch_ud, b + 1u, nb);
                }
            }
        }
    }
    if (!chunked_gpu) npc_svo_grid_destroy(&svo);
    return true;
}

void lm_mesh_bake_readback_sh(const lm_mesh_bake_result_t *result,
                              uint32_t coeff, float *out_rgb)
{
    uint32_t w = result->atlas.width, h = result->atlas.height;
    for (uint32_t i = 0; i < w * h * 3; ++i)
        out_rgb[i] = 0.0f;
    if (coeff >= 9u)
        return;
    for (uint32_t i = 0; i < result->n_luxels; ++i) {
        const lm_luxel_t *lx = &result->combined.luxels[i];
        uint32_t px = result->atlas_x[i], py = result->atlas_y[i];
        float *dst = &out_rgb[(py * w + px) * 3];
        dst[0] = lx->sh[0].c[coeff];
        dst[1] = lx->sh[1].c[coeff];
        dst[2] = lx->sh[2].c[coeff];
    }
}
