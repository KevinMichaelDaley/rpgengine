/**
 * @file gi_runtime.c
 * @brief Dynamic-light SDF-probe GI runtime (see gi_runtime.h).
 */
#include "ferrum/renderer/gi/gi_runtime.h"

#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/gi/gi_probe_place.h"
#include "ferrum/renderer/light.h"
#include "ferrum/renderer/light_store.h"

#ifdef TRACY_ENABLE
#include "tracy/TracyC.h"
#define GI_ZONE(v, name) TracyCZoneN(v, name, true)
#define GI_ZONE_END(v) TracyCZoneEnd(v)
#else
#define GI_ZONE(v, name)
#define GI_ZONE_END(v)
#endif

#define GI_MAX_PROBES 65536u

bool gi_runtime_init(gi_runtime_t *gi, const gi_runtime_config_t *cfg)
{
    if (gi == NULL || cfg == NULL || cfg->loader == NULL ||
        (cfg->sdf_prefix == NULL && cfg->ext_sdf == NULL))
        return false;
    memset(gi, 0, sizeof *gi);
    gi->soft_k = cfg->soft_k > 0.0f ? cfg->soft_k : 8.0f;
    gi->update_interval = cfg->update_interval > 0 ? cfg->update_interval : 8;
    { const char *e = getenv("GI_UPDATE");
      if (e != NULL) { int v = atoi(e); if (v >= 1) gi->update_interval = v; } }
    /* Staggered probe updates (rpg-iuiy): spread the per-update cone-trace over
     * n_groups frames -- a spatially-DITHERED 1/n slice each frame. NOTE: profiling
     * the great hall showed the probe trace is NOT the fps bottleneck (1200 vs 72
     * probes: same fps; the forward PBR shading dominates), and dispatching every
     * frame instead of every update_interval ADDS overhead -- so this defaults OFF
     * (1). It stays available for frame-time smoothing / scaling to many more
     * probes+lights, via cfg or GI_PROBE_GROUPS. */
    gi->n_groups = cfg->n_probe_groups > 0 ? cfg->n_probe_groups : 1;
    { const char *e = getenv("GI_PROBE_GROUPS");
      if (e != NULL) { int v = atoi(e); if (v >= 1) gi->n_groups = v; } }
    if (gi->n_groups < 1) gi->n_groups = 1;
    /* Probe specular SG lobes summed per fragment (each = 8 corners * 2 texelFetch
     * + exp). 2 by default: ~all the fps of 1 lobe (26.2 vs 26.9, vs 22.9 for 3)
     * while keeping the multi-lobe quality 1 loses. GI_SG_LOBES overrides. */
    gi->spec_lobes = cfg->tuning.spec_lobes > 0 ? cfg->tuning.spec_lobes : 2;
    { const char *e = getenv("GI_SG_LOBES");
      if (e != NULL) { int v = atoi(e); if (v >= 0) gi->spec_lobes = v; } }
    if (gi->spec_lobes < 0) gi->spec_lobes = 0;
    if (gi->spec_lobes > 3) gi->spec_lobes = 3;
    /* Steady-state temporal EMA blend per probe update. Smaller = each update
     * nudges less -> smoother, less "fast shifting" as checkerboard groups cycle
     * (at the cost of slower convergence, fine for low-freq GI). GI_SMOOTH env. */
    gi->smooth = (cfg->smooth > 0.0f && cfg->smooth <= 1.0f) ? cfg->smooth : 0.15f;
    { const char *e = getenv("GI_SMOOTH");
      if (e != NULL) { float v = (float)atof(e); if (v > 0.0f && v <= 1.0f) gi->smooth = v; } }
    /* Static-indirect weights (rpg-pau4): baked surfaces get a mild extra bounce,
     * dynamic objects get the full static ambience. Overridable per demo. */
    gi->static_baked_w = 0.35f;
    gi->static_dyn_w = 1.0f;
    gi->sky_ao_ref = 6.0f;   /* sky_ao_color defaults to 0 (off) until set. */
    /* Probe-visibility softening (dot-artifact fix at dynamic-occluder edges).
     * Defaults are softer than the old hardcoded (0.15 bias, 1e-3 var, squared)
     * so the per-probe light/dark transition blends instead of stamping the grid. */
    gi->vis_bias = cfg->tuning.vis_bias > 0.0f ? cfg->tuning.vis_bias : 0.30f;
    { const char *e = getenv("GI_VIS_BIAS"); if (e) { float v=(float)atof(e); if (v>=0.0f) gi->vis_bias=v; } }
    gi->vis_varmin = cfg->tuning.vis_varmin > 0.0f ? cfg->tuning.vis_varmin : 0.02f;
    { const char *e = getenv("GI_VIS_VARMIN"); if (e) { float v=(float)atof(e); if (v>0.0f) gi->vis_varmin=v; } }
    gi->vis_sharp = cfg->tuning.vis_sharp > 0.0f ? cfg->tuning.vis_sharp : 1.0f;
    { const char *e = getenv("GI_VIS_SHARP"); if (e) { float v=(float)atof(e); if (v>0.0f) gi->vis_sharp=v; } }

    /* --- Baked SDF residency: borrow an external (streamed) SDF stream if given
     * (rpg-c7fk), else self-load all chunks from the prefix. --- */
    if (cfg->ext_sdf != NULL) {
        gi->sdf_ptr = cfg->ext_sdf;
        gi->sdf_owned = 0;
    } else {
        if (gi_sdf_stream_load(&gi->sdf, cfg->sdf_prefix) <= 0) {
            fprintf(stderr, "gi_runtime: no SDF chunks at %s\n", cfg->sdf_prefix);
            return false;
        }
        gi->sdf_ptr = &gi->sdf;
        gi->sdf_owned = 1;
    }
    gi->n_sdf_boxes = gi_sdf_stream_boxes(gi->sdf_ptr, gi->box_min, gi->box_max);

    /* --- Adaptive probes seeded over the play volume + accel grid. --- */
    gi->probe_pos = malloc((size_t)GI_MAX_PROBES * 3 * sizeof(float));
    gi->probe_sh = malloc((size_t)GI_MAX_PROBES * 27 * sizeof(float));
    if (gi->probe_pos == NULL || gi->probe_sh == NULL) { gi_runtime_destroy(gi); return false; }
    gi_probe_set_init(&gi->probes, gi->probe_pos, gi->probe_sh, GI_MAX_PROBES);
    float spacing = cfg->probe_spacing > 0.0f ? cfg->probe_spacing : 1.0f;
    if (cfg->probe_pos_in != NULL && cfg->n_probe_in > 0) {
        /* Manual adaptive placement (caller-specified, scene-tuned positions). */
        for (uint32_t i = 0; i < cfg->n_probe_in; ++i)
            gi_probe_add(&gi->probes, cfg->probe_pos_in[i*3+0],
                         cfg->probe_pos_in[i*3+1], cfg->probe_pos_in[i*3+2]);
        fprintf(stderr, "gi_runtime: %u manual probes\n", gi->probes.count);
    } else {
        /* Auto: seed a lattice, then prune to probes near a baked surface. */
        gi_probe_seed_box(&gi->probes, cfg->aabb_min, cfg->aabb_max, spacing);
        uint32_t seeded = gi->probes.count; float band = spacing * 0.9f; uint32_t keep = 0;
        for (uint32_t i = 0; i < gi->probes.count; ++i) {
            const float *p = &gi->probe_pos[i * 3];
            float d = gi_sdf_stream_sample(gi->sdf_ptr, p);
            if (d > 0.05f && d < band) {
                if (keep != i) { gi->probe_pos[keep*3+0]=p[0]; gi->probe_pos[keep*3+1]=p[1]; gi->probe_pos[keep*3+2]=p[2]; }
                ++keep;
            }
        }
        gi->probes.count = keep;
        fprintf(stderr, "gi_runtime: probes pruned %u -> %u\n", seeded, keep);
    }
    if (gi->probes.count == 0) { fprintf(stderr, "gi_runtime: 0 probes\n"); gi_runtime_destroy(gi); return false; }

    /* Probe backing capacity: room for runtime set-probe updates (streamed / per-
     * zone sets) without reallocation. 0 => fixed at the init count. */
    gi->probe_cap = (cfg->max_probes > gi->probes.count) ? cfg->max_probes : gi->probes.count;
    if (gi->probe_cap > GI_MAX_PROBES) gi->probe_cap = GI_MAX_PROBES;
    for (int a = 0; a < 3; ++a) { gi->gi_aabb_min[a] = cfg->aabb_min[a]; gi->gi_aabb_max[a] = cfg->aabb_max[a]; }

    float cell = cfg->grid_cell > 0.0f ? cfg->grid_cell : (spacing * 2.0f);
    gi->gi_cell = cell;
    /* Grid cell count for the accel structure. */
    int gd[3];
    for (int a = 0; a < 3; ++a) {
        float span = cfg->aabb_max[a] - cfg->aabb_min[a];
        int d = (int)(span / cell); if ((float)d * cell < span) d++; gd[a] = d < 1 ? 1 : d;
    }
    uint32_t ncells = (uint32_t)gd[0] * (uint32_t)gd[1] * (uint32_t)gd[2];
    gi->gi_ncells = ncells;
    gi->cell_start = malloc((size_t)(ncells + 1) * sizeof(uint32_t));
    gi->probe_idx = malloc((size_t)gi->probe_cap * sizeof(uint32_t));
    if (gi->cell_start == NULL || gi->probe_idx == NULL) { gi_runtime_destroy(gi); return false; }
    if (!gi_probe_grid_build(&gi->grid, &gi->probes, cfg->aabb_min, cfg->aabb_max,
                             cell, gi->cell_start, ncells + 1u, gi->probe_idx, gi->probes.count)) {
        fprintf(stderr, "gi_runtime: accel grid build failed\n"); gi_runtime_destroy(gi); return false;
    }

    /* --- GPU compute + probe upload. --- */
    gi->max_lights = cfg->max_lights ? cfg->max_lights : 512u;
    gi->light_scratch = malloc((size_t)gi->max_lights * sizeof(gi_light_t));
    if (gi->light_scratch == NULL) { gi_runtime_destroy(gi); return false; }
    if (!gi_probe_gpu_init(&gi->gpu, cfg->loader, gi->probe_cap,
                           gi->max_lights, cfg->max_boxes ? cfg->max_boxes : 64u)) {
        gi_runtime_destroy(gi); return false;
    }
    gi_probe_gpu_set_probes(&gi->gpu, gi->probe_pos, gi->probes.count);

    /* --- Accel grid as texture buffers for the forward+ sampler. --- */
    glGenBuffers(1, &gi->tbo_cs); glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_cs);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(ncells + 1) * sizeof(uint32_t), gi->cell_start, GL_DYNAMIC_DRAW);
    glGenTextures(1, &gi->tbo_cs_tex); glBindTexture(GL_TEXTURE_BUFFER, gi->tbo_cs_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, gi->tbo_cs);
    glGenBuffers(1, &gi->tbo_pi); glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_pi);
    /* Capacity-sized (probe_cap) so runtime set-probe updates fit; count valid. */
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)gi->probe_cap * sizeof(uint32_t), NULL, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)gi->probes.count * sizeof(uint32_t), gi->probe_idx);
    glGenTextures(1, &gi->tbo_pi_tex); glBindTexture(GL_TEXTURE_BUFFER, gi->tbo_pi_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, gi->tbo_pi);

    /* --- Probe froxel grid: bins probes into the SAME clusters the forward+
     * lights use (config MUST match fcfg.cluster), so the material samples probe
     * candidates from the fragment's own froxel via the forward+ cluster uniforms. */
    gi->probe_min = cfg->probe_min > 0 ? cfg->probe_min : 4u;
    gi->probe_sphere_margin = cfg->probe_sphere_margin > 0.0f ? cfg->probe_sphere_margin : 1.5f;
    gi->bin_interval = cfg->bin_interval > 0 ? cfg->bin_interval : 1;
    gi_probe_gpu_set_tuning(&gi->gpu, &cfg->tuning);
    {
        cluster_config_t fc = cfg->froxel;
        if (fc.tiles_x == 0) fc = (cluster_config_t){ 16, 16, 24, 0.2f, 60.0f };
        uint32_t ctot = fc.tiles_x * fc.tiles_y * fc.slices;
        uint32_t per = gi->probe_cap < 64u ? gi->probe_cap : 64u;   /* size for the cap. */
        uint32_t icap = ctot * (per < 1u ? 1u : per);
        gi->fx_off = malloc((size_t)ctot * sizeof(uint32_t));
        gi->fx_cnt = malloc((size_t)ctot * sizeof(uint32_t));
        gi->fx_idx = malloc((size_t)icap * sizeof(uint32_t));
        if (gi->fx_off == NULL || gi->fx_cnt == NULL || gi->fx_idx == NULL) {
            gi_runtime_destroy(gi); return false;
        }
        cluster_grid_init(&gi->froxel, fc, gi->fx_off, gi->fx_cnt, gi->fx_idx, icap);
        glGenBuffers(1, &gi->tbo_fo); glGenTextures(1, &gi->tbo_fo_tex);
        glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_fo);
        glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)ctot * sizeof(uint32_t), NULL, GL_DYNAMIC_DRAW);
        glBindTexture(GL_TEXTURE_BUFFER, gi->tbo_fo_tex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, gi->tbo_fo);
        glGenBuffers(1, &gi->tbo_fc); glGenTextures(1, &gi->tbo_fc_tex);
        glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_fc);
        glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)ctot * sizeof(uint32_t), NULL, GL_DYNAMIC_DRAW);
        glBindTexture(GL_TEXTURE_BUFFER, gi->tbo_fc_tex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, gi->tbo_fc);
        glGenBuffers(1, &gi->tbo_fi); glGenTextures(1, &gi->tbo_fi_tex);
        glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_fi);
        glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)icap * sizeof(uint32_t), NULL, GL_DYNAMIC_DRAW);
        glBindTexture(GL_TEXTURE_BUFFER, gi->tbo_fi_tex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, gi->tbo_fi);
    }

    /* --- SDF vis prepass. --- */
    if (gi_vis_prepass_init(&gi->pp, cfg->prepass_w > 0 ? cfg->prepass_w : 240,
                            cfg->prepass_h > 0 ? cfg->prepass_h : 135,
                            gi->sdf_ptr->n_chunks) != 0) {
        gi_runtime_destroy(gi); return false;
    }
    fprintf(stderr, "gi_runtime: %u probes, %d SDF chunks, accel %dx%dx%d\n",
            gi->probes.count, gi->sdf_ptr->n_chunks, gd[0], gd[1], gd[2]);
    gi->ready = true;
    return true;
}

void gi_runtime_set_probes(gi_runtime_t *gi, const float *pos, uint32_t count)
{
    if (gi == NULL || pos == NULL || !gi->ready) return;
    if (count > gi->probe_cap) count = gi->probe_cap;

    /* Refill the probe set (backing = gi->probe_pos, capacity GI_MAX_PROBES). */
    gi_probe_set_reset(&gi->probes);
    for (uint32_t i = 0; i < count; ++i)
        gi_probe_add(&gi->probes, pos[i*3+0], pos[i*3+1], pos[i*3+2]);
    if (gi->probes.count == 0) return;

    /* Rebuild the world accel grid over the same bounds/cell + re-upload it and
     * the probe positions. The per-frame froxel binning picks up the new set. */
    gi_probe_grid_build(&gi->grid, &gi->probes, gi->gi_aabb_min, gi->gi_aabb_max,
                        gi->gi_cell, gi->cell_start, gi->gi_ncells + 1u,
                        gi->probe_idx, gi->probes.count);
    gi_probe_gpu_set_probes(&gi->gpu, gi->probe_pos, gi->probes.count);
    glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_cs);
    glBufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)(gi->gi_ncells + 1) * sizeof(uint32_t), gi->cell_start);
    glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_pi);
    glBufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)gi->probes.count * sizeof(uint32_t), gi->probe_idx);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void gi_runtime_set_visible(gi_runtime_t *gi, const uint8_t *visible, int n_chunks)
{
    if (gi == NULL) return;
    gi->ext_visible = visible;
    gi->ext_visible_n = n_chunks;
}

void gi_runtime_frame(gi_runtime_t *gi, const render_scene_t *scene,
                      const float view[16], const float proj[16],
                      const gi_collider_t *boxes, uint32_t n_boxes,
                      int main_w, int main_h)
{
    if (gi == NULL || !gi->ready || scene == NULL) return;

    /* Re-bin probes into the forward+ froxels. Camera-dependent, so this runs
     * independently of the (slower) probe-SH update below; probes are not static,
     * and the froxel assignment tracks both moving probes and the moving camera.
     * bin_interval lets it skip frames when the view barely changes.
     *
     * SKIP ENTIRELY when the regular-grid trilinear path is active
     * (probe_grid_on): in that mode the PBR shader interpolates the 8 surrounding
     * GRID probes and returns before it ever reads the froxel probe lists
     * (u_probe_froxel_off/cnt/idx), so binning them is pure dead work. The bin is
     * O(clusters x probes x 2) on the CPU every frame (e.g. 16x16x24 froxels x
     * 1024 probes ~= 12.6M iterations) -- the dominant per-frame cost at high probe
     * counts. The froxel TBOs stay validly allocated (bound but never sampled), so
     * leaving them un-updated is safe. Only the nearest-froxel fallback needs them. */
    if (!gi->probe_grid_on && gi->frame_counter % gi->bin_interval == 0) {
        GI_ZONE(z_bin, "Game.GI.FroxelBin");
        render_camera_t cam;
        memset(&cam, 0, sizeof cam);
        for (int i = 0; i < 16; ++i) { cam.view[i] = view[i]; cam.proj[i] = proj[i]; }
        cluster_grid_build_points(&gi->froxel, &cam, gi->probe_pos,
                                  gi->probes.count, gi->probe_min,
                                  gi->probe_sphere_margin);
        GI_ZONE_END(z_bin);
        uint32_t ctot = gi->froxel.cluster_total;
        uint32_t nidx = gi->froxel.index_count > 0 ? gi->froxel.index_count : 1u;
        glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_fo);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)ctot * sizeof(uint32_t), gi->fx_off);
        glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_fc);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)ctot * sizeof(uint32_t), gi->fx_cnt);
        glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_fi);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)nidx * sizeof(uint32_t), gi->fx_idx);
    }

    /* GI is low-frequency. Two cadences:
     *  - PAGING (SDF residency + light gather): the coarse update_interval, since
     *    residency and the light set change slowly.
     *  - PROBE TRACE: when staggering (n_groups>1) it runs EVERY frame on a
     *    dithered 1/n slice so the ~1200-probe cost is amortised; otherwise it
     *    runs only on the coarse update frame (legacy all-at-once). */
    /* Dispatch (and page) only every update_interval frames. Each such "tick"
     * traces one dithered group of probes -- so update_interval is the FLUSH
     * cadence (the compute->graphics tile-buffer flush is the dominant cost on a
     * tiled GPU), and n_groups is how many ticks cover all probes. A probe
     * refreshes every update_interval*n_groups frames. Fewer, larger ticks =
     * fewer flushes (faster) but bigger per-tick bursts; more, smaller ticks =
     * smoother. */
    uint32_t frame = (uint32_t)gi->frame_counter++;
    if (frame % (uint32_t)gi->update_interval != 0)
        return;
    uint32_t tick = frame / (uint32_t)gi->update_interval;
    int K = gi->n_groups > 1 ? gi->n_groups : 1;
    {
        /* Page the on-screen SDF chunks. Use the external (shared dual-prepass)
         * visible mask if the client provides one; else run the internal world
         * prepass (per-fragment world-pos classification). */
        if (gi->ext_visible != NULL) {
            gi_sdf_stream_page(gi->sdf_ptr, gi->ext_visible);
        } else {
            gi_vis_prepass_run_world(&gi->pp, scene, view, proj, gi->box_min, gi->box_max,
                                     gi->n_sdf_boxes, main_w, main_h);
            gi_sdf_stream_page(gi->sdf_ptr, gi->pp.visible);
        }
    }

    /* Gather the scene lights tagged PROBE_GI into the trace's light set: the
     * probes trace indirect from ONLY these (a static light like the sun can opt
     * in without becoming a realtime direct light; dynamic lights can opt out). */
    uint32_t n = 0;
    const render_light_store_t *ls = scene->lights;
    if (ls != NULL) {
        for (uint32_t i = 0; i < ls->count && n < gi->max_lights; ++i) {
            const render_light_t *L = &ls->lights[i];
            if (!(L->flags & RENDER_LIGHT_FLAG_PROBE_GI)) continue;
            gi_light_t *g = &gi->light_scratch[n++];
            g->kind = (L->kind == RENDER_LIGHT_DIRECTIONAL) ? GI_LIGHT_DIRECTIONAL
                    : (L->kind == RENDER_LIGHT_SPOT) ? GI_LIGHT_SPOT : GI_LIGHT_POINT;
            for (int a = 0; a < 3; ++a) { g->pos[a] = L->position[a]; g->dir[a] = L->direction[a]; }
            for (int a = 0; a < 3; ++a) g->color[a] = L->color[a] * L->intensity;
            g->range = L->range; g->cos_inner = L->cos_inner; g->cos_outer = L->cos_outer;
        }
    }
    /* March every probe to every tagged light through the resident combined SDF. */
    /* Temporal EMA of the probe coefficients + visibility: full replace on the
     * first update (buffers start uninitialised), then blend so a moving occluder
     * fades in/out instead of snapping. frame_counter==1 right after the 1st. */
    /* Staggered slice this frame; full replace until every group has debuted once
     * (buffers start uninitialised: frames 0..K-1), then blend so a moving
     * occluder fades rather than snaps. */
    int group = (K > 1) ? (int)(tick % (uint32_t)K) : 0;
    /* Full replace until every group has debuted, then a SMALL blend so each
     * (checkerboard-scattered) update nudges the probe gently -- avoids the GI
     * visibly "shifting fast" as groups cycle. gi->smooth is the steady blend. */
    float temporal = (tick < (uint32_t)K) ? 1.0f : gi->smooth;
    gi_probe_gpu_dispatch(&gi->gpu, gi->sdf_ptr, gi->light_scratch, n, boxes, n_boxes,
                          gi->soft_k, temporal, K, group, gi->probe_grid_dim,
                          gi->probe_grid_origin, gi->probe_grid_cell);
}

void gi_runtime_bind(const gi_runtime_t *gi, shader_uniform_cache_t *cache,
                     const shader_program_t *program, uint32_t base_unit)
{
    if (gi == NULL || !gi->ready || cache == NULL || program == NULL) return;
    uint32_t u = base_unit;
    glActiveTexture(GL_TEXTURE0 + u);   glBindTexture(GL_TEXTURE_BUFFER, gi->gpu.tbo_pos_tex);
    shader_uniform_set_int(cache, program, "u_probe_pos", (int32_t)u); ++u;
    glActiveTexture(GL_TEXTURE0 + u);   glBindTexture(GL_TEXTURE_BUFFER, gi->gpu.tbo_sh_tex);
    shader_uniform_set_int(cache, program, "u_probe_sh", (int32_t)u); ++u;
    /* Probe froxel lists (same clusters as the forward+ lights). The shader
     * recomputes the fragment's cluster from the forward+ cluster uniforms and
     * reads its probe candidates from these. */
    glActiveTexture(GL_TEXTURE0 + u);   glBindTexture(GL_TEXTURE_BUFFER, gi->tbo_fo_tex);
    shader_uniform_set_int(cache, program, "u_probe_froxel_off", (int32_t)u); ++u;
    glActiveTexture(GL_TEXTURE0 + u);   glBindTexture(GL_TEXTURE_BUFFER, gi->tbo_fc_tex);
    shader_uniform_set_int(cache, program, "u_probe_froxel_cnt", (int32_t)u); ++u;
    glActiveTexture(GL_TEXTURE0 + u);   glBindTexture(GL_TEXTURE_BUFFER, gi->tbo_fi_tex);
    shader_uniform_set_int(cache, program, "u_probe_froxel_idx", (int32_t)u); ++u;
    /* Depth is now a GL_LINEAR RG32F 2D-array (hardware-filtered Chebyshev). */
    glActiveTexture(GL_TEXTURE0 + u);   glBindTexture(GL_TEXTURE_2D, gi->gpu.depth_arr);
    shader_uniform_set_int(cache, program, "u_probe_depth", (int32_t)u); ++u;
    glActiveTexture(GL_TEXTURE0 + u);   glBindTexture(GL_TEXTURE_BUFFER, gi_probe_gpu_sg_tbo(&gi->gpu));
    shader_uniform_set_int(cache, program, "u_probe_sg", (int32_t)u); ++u;

    /* Brick sampling structure (rpg-pjkb): O(1) voxel -> brick -> 8 probes.
     * When absent the shader falls back to the froxel path. */
    shader_uniform_set_int(cache, program, "u_brick_on", gi->bricks.on);
    if (gi->bricks.on) {
        glActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_3D, gi->bricks.index_tex);
        shader_uniform_set_int(cache, program, "u_brick_index", (int32_t)u); ++u;
        glActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_BUFFER, gi->bricks.meta_tex);
        shader_uniform_set_int(cache, program, "u_brick_meta", (int32_t)u); ++u;
        glActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_BUFFER, gi->bricks.pidx_tex);
        shader_uniform_set_int(cache, program, "u_brick_pidx", (int32_t)u); ++u;
        glActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_BUFFER, gi->bricks.valid_tex);
        shader_uniform_set_int(cache, program, "u_probe_valid", (int32_t)u); ++u;
        shader_uniform_set_vec3(cache, program, "u_brick_origin", gi->bricks.origin);
        shader_uniform_set_float(cache, program, "u_brick_voxel", gi->bricks.voxel);
        {
            float d3[3] = { (float)gi->bricks.dim[0], (float)gi->bricks.dim[1],
                            (float)gi->bricks.dim[2] };
            shader_uniform_set_vec3(cache, program, "u_brick_dim", d3);
        }
    }

    shader_uniform_set_int(cache, program, "u_gi_enabled", 1);
    shader_uniform_set_float(cache, program, "u_gi_static_baked_w", gi->static_baked_w);
    shader_uniform_set_float(cache, program, "u_gi_static_dyn_w", gi->static_dyn_w);
    shader_uniform_set_int(cache, program, "u_probe_grid_on", gi->probe_grid_on);
    if (gi->probe_grid_on) {
        float dimf[3] = { (float)gi->probe_grid_dim[0], (float)gi->probe_grid_dim[1],
                          (float)gi->probe_grid_dim[2] };
        shader_uniform_set_vec3(cache, program, "u_probe_grid_origin", gi->probe_grid_origin);
        shader_uniform_set_vec3(cache, program, "u_probe_grid_cell", gi->probe_grid_cell);
        shader_uniform_set_vec3(cache, program, "u_probe_grid_dim", dimf);
    }
    shader_uniform_set_vec3(cache, program, "u_gi_sky_color", gi->sky_ao_color);
    shader_uniform_set_float(cache, program, "u_gi_sky_ref", gi->sky_ao_ref);
    shader_uniform_set_float(cache, program, "u_gi_spec_gain", gi->spec_gain);
    shader_uniform_set_int(cache, program, "u_gi_spec_lobes", gi->spec_lobes);
    shader_uniform_set_float(cache, program, "u_gi_ao_mult", gi->ao_mult);
    shader_uniform_set_float(cache, program, "u_gi_vis_bias", gi->vis_bias);
    shader_uniform_set_float(cache, program, "u_gi_vis_varmin", gi->vis_varmin);
    shader_uniform_set_float(cache, program, "u_gi_vis_sharp", gi->vis_sharp);
}

void gi_runtime_destroy(gi_runtime_t *gi)
{
    if (gi == NULL) return;
    gi_vis_prepass_destroy(&gi->pp);
    if (gi->sdf_owned) gi_sdf_stream_destroy(&gi->sdf);  /* external stream: not ours. */
    gi_probe_gpu_destroy(&gi->gpu);
    if (gi->tbo_cs) glDeleteBuffers(1, &gi->tbo_cs);
    if (gi->tbo_cs_tex) glDeleteTextures(1, &gi->tbo_cs_tex);
    if (gi->tbo_pi) glDeleteBuffers(1, &gi->tbo_pi);
    if (gi->tbo_pi_tex) glDeleteTextures(1, &gi->tbo_pi_tex);
    if (gi->tbo_fo) glDeleteBuffers(1, &gi->tbo_fo);
    if (gi->tbo_fo_tex) glDeleteTextures(1, &gi->tbo_fo_tex);
    if (gi->tbo_fc) glDeleteBuffers(1, &gi->tbo_fc);
    if (gi->tbo_fc_tex) glDeleteTextures(1, &gi->tbo_fc_tex);
    if (gi->tbo_fi) glDeleteBuffers(1, &gi->tbo_fi);
    if (gi->tbo_fi_tex) glDeleteTextures(1, &gi->tbo_fi_tex);
    free(gi->fx_off); free(gi->fx_cnt); free(gi->fx_idx);
    free(gi->probe_pos); free(gi->probe_sh);
    free(gi->cell_start); free(gi->probe_idx);
    gi_brick_gpu_destroy(&gi->bricks);
    free(gi->light_scratch);
    memset(gi, 0, sizeof *gi);
}
