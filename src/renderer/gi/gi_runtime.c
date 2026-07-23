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
#include "ferrum/probe/probe_sh_file.h"
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

#define GI_MAX_PROBES 1000000u

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
    /* Bake-and-freeze horizon (0 = keep updating). GI_FREEZE env overrides. */
    gi->freeze_ticks = cfg->freeze_ticks > 0 ? cfg->freeze_ticks : 0;
    { const char *e = getenv("GI_FREEZE");
      if (e != NULL) { int v = atoi(e); if (v >= 0) gi->freeze_ticks = v; } }
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
    gi->n_sdf_boxes = gi_sdf_stream_boxes(gi->sdf_ptr, gi->box_min, gi->box_max,
                                          GI_VIS_MAX_BOXES);

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

    /* PRECOMPUTED probe irradiance from the offline baker: upload it straight
     * into the SH/SG buffers and freeze -- zero runtime convergence, strong
     * clean indirect (baked at high quality). Requires a matching probe count
     * (same .probes order). Otherwise fall through to runtime convergence. */
    if (gi->sdf_ptr != NULL && gi->sdf_ptr->has_probesh) {
        /* Streamed baked SH: freeze now; the per-chunk SH uploads as chunks
         * page in (above). The field appears progressively, like the lightmap. */
        gi->frozen = 1;
        fprintf(stderr, "gi_runtime: streamed BAKED probe SH -- frozen.\n");
    } else if (cfg->baked_sh != NULL && cfg->baked_sg != NULL &&
        cfg->n_baked == gi->probes.count) {
        gi_probe_gpu_upload(&gi->gpu, cfg->baked_sh, cfg->baked_sg);
        gi->frozen = 1;
        fprintf(stderr, "gi_runtime: loaded BAKED probe SH (%u probes) -- frozen.\n",
                cfg->n_baked);
    } else if (cfg->baked_sh != NULL && cfg->n_baked != gi->probes.count) {
        fprintf(stderr, "gi_runtime: baked SH probe count %u != %u -- ignoring, "
                "will converge at runtime.\n", cfg->n_baked, gi->probes.count);
    }

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
    GI_ZONE(z_gif, "Game.GI.Frame");
    if (gi == NULL || !gi->ready || scene == NULL) { GI_ZONE_END(z_gif); return; }

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
    if (!gi->probe_grid_on && !gi->bricks.on &&
        gi->frame_counter % gi->bin_interval == 0) {
        /* Also skipped under BRICK sampling (adaptive probe bakes): the shader
         * resolves probes through u_brick_index in O(1) and never touches the
         * froxel lists, so the O(clusters x probes) CPU bin is dead work there
         * exactly as it is for the regular grid. */
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
    /* Streamed baked probe SH (piggybacked on the SDF chunk residency): each
     * frame, push any chunk whose .probesh RAM is loaded but not yet on the GPU
     * into the probe buffer. Runs even when frozen -- it is how the frozen field
     * fills in as SDF chunks page by visibility (bounded RAM, like the lightmap). */
    if (gi->sdf_ptr != NULL && gi->sdf_ptr->has_probesh) {
        gi_sdf_stream_t *s = gi->sdf_ptr;
        for (int c = 0; c < s->n_chunks; ++c) {
            if (gi_sdf_stream_probes_uploaded(s, c)) continue;
            const uint32_t *idx = NULL; const float *sh = NULL, *sg = NULL; uint32_t nc = 0;
            if (!gi_sdf_stream_chunk_probes(s, c, &idx, &sh, &sg, &nc) || nc == 0) continue;
            gi_probe_gpu_upload_indexed(&gi->gpu, idx, sh, sg, nc);
            gi_sdf_stream_mark_probes_uploaded(s, c);
        }
    }

    /* BAKE-AND-FREEZE: once the probe field has converged for a static sun/world
     * (or precomputed SH was loaded / is streaming), stop dispatching entirely --
     * the probe buffers keep their baked coefficients and are only sampled. Zero
     * per-frame GI cost. gi_runtime_bind still binds the buffers each frame. */
    if (gi->frozen)
        { GI_ZONE_END(z_gif); return; }
    uint32_t frame = (uint32_t)gi->frame_counter++;
    if (frame % (uint32_t)gi->update_interval != 0)
        { GI_ZONE_END(z_gif); return; }
    uint32_t tick = frame / (uint32_t)gi->update_interval;
    int K = gi->n_groups > 1 ? gi->n_groups : 1;
    /* Reached the freeze horizon? Do this final tick, then freeze from the next
     * frame on. freeze_ticks should be >= a few * K so every group traced and
     * the temporal EMA settled. */
    if (gi->freeze_ticks > 0 && (int)tick >= gi->freeze_ticks) {
        gi->frozen = 1;
        fprintf(stderr, "gi_runtime: probe field FROZEN after %u ticks (baked).\n", tick);
    }
    {
        /* Page the on-screen SDF chunks. Use the external (shared dual-prepass)
         * visible mask if the client provides one; else run the internal world
         * prepass (per-fragment world-pos classification). */
        /* Camera position from the view matrix (column-major): cam = -R^T t,
         * so residency can converge on the nearest chunks instead of LRU-
         * rotating through an over-pool visible set. */
        float cam[3] = {
            -(view[0]*view[12] + view[1]*view[13] + view[2]*view[14]),
            -(view[4]*view[12] + view[5]*view[13] + view[6]*view[14]),
            -(view[8]*view[12] + view[9]*view[13] + view[10]*view[14]),
        };
        if (gi->ext_visible != NULL) {
            gi_sdf_stream_page(gi->sdf_ptr, gi->ext_visible, cam);
        } else {
            gi_vis_prepass_run_world(&gi->pp, scene, view, proj, gi->box_min, gi->box_max,
                                     gi->n_sdf_boxes, main_w, main_h);
            gi_sdf_stream_page(gi->sdf_ptr, gi->pp.visible, cam);
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
    GI_ZONE_END(z_gif);
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
     * When absent the shader falls back to the froxel path -- but the brick
     * SAMPLERS still get their own units: an unassigned isampler3D /
     * samplerBuffer defaults to unit 0, type-clashing with the albedo
     * sampler2D and making EVERY draw INVALID_OPERATION ("program texture
     * usage"). A brickless level (e.g. la_sprawl) rendered pure clear colour
     * because of exactly this. Textures are only bound when bricks are on. */
    shader_uniform_set_int(cache, program, "u_brick_on", gi->bricks.on);
    glActiveTexture(GL_TEXTURE0 + u);
    glBindTexture(GL_TEXTURE_3D, gi->bricks.on ? gi->bricks.index_tex : 0);
    shader_uniform_set_int(cache, program, "u_brick_index", (int32_t)u); ++u;
    glActiveTexture(GL_TEXTURE0 + u);
    glBindTexture(GL_TEXTURE_BUFFER, gi->bricks.on ? gi->bricks.meta_tex : 0);
    shader_uniform_set_int(cache, program, "u_brick_meta", (int32_t)u); ++u;
    glActiveTexture(GL_TEXTURE0 + u);
    glBindTexture(GL_TEXTURE_BUFFER, gi->bricks.on ? gi->bricks.pidx_tex : 0);
    shader_uniform_set_int(cache, program, "u_brick_pidx", (int32_t)u); ++u;
    glActiveTexture(GL_TEXTURE0 + u);
    glBindTexture(GL_TEXTURE_BUFFER, gi->bricks.on ? gi->bricks.valid_tex : 0);
    shader_uniform_set_int(cache, program, "u_probe_valid", (int32_t)u); ++u;
    if (gi->bricks.on) {
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

uint32_t gi_runtime_probe_count(const gi_runtime_t *gi)
{
    return gi != NULL ? gi->probes.count : 0u;
}

/* Nearest SDF chunk to a probe (0 distance if inside a box). Assigns EVERY
 * probe to exactly one chunk so none is orphaned (probes in chunk padding /
 * gaps still trace + stream). */
static int bake_nearest_chunk(const gi_sdf_stream_t *s, const float *pos)
{
    int best = -1; float bestd = 1e30f;
    for (int c = 0; c < s->n_chunks; ++c) {
        float d2 = 0.0f;
        for (int a = 0; a < 3; ++a) {
            float mn = s->ram[c].origin[a];
            float mx = mn + (float)s->ram[c].dims[a] * s->ram[c].voxel;
            float v = pos[a] < mn ? mn - pos[a] : (pos[a] > mx ? pos[a] - mx : 0.0f);
            d2 += v * v;
        }
        if (d2 < bestd) { bestd = d2; best = c; }
    }
    return best;
}

void gi_runtime_bake_converge(gi_runtime_t *gi, const render_scene_t *scene,
                              const char *sdf_prefix, int iters)
{
    if (gi == NULL || scene == NULL || sdf_prefix == NULL ||
        gi->sdf_ptr == NULL || gi->probes.count == 0)
        return;
    if (iters < 1) iters = 8;
    gi_sdf_stream_t *s = gi->sdf_ptr;
    /* Force ALL chunk SDF into RAM up front (the streamer normally pages it on
     * a fiber; the bake is offline and can hold the whole set). Paging below
     * then just uploads the resident subset to the GPU cache per chunk. */
    int loaded = 0;
    for (int c = 0; c < s->n_chunks; ++c)
        if (gi_sdf_stream_chunk_load(s, c, sdf_prefix) > 0 || s->ram[c].dist != NULL)
            ++loaded;
    int saved_cap = s->max_uploads;
    s->max_uploads = 0;   /* UNCAP: page must upload the whole neighbourhood per
                           * chunk, else probe rays escape into empty slots. */
    fprintf(stderr, "[bake] loaded %d/%d SDF chunks to RAM\n", loaded, s->n_chunks);
    uint32_t np = gi->probes.count;
    uint8_t *active = malloc(np);
    uint8_t *visible = malloc((size_t)s->n_chunks);
    if (active == NULL || visible == NULL) { free(active); free(visible); return; }

    /* Gather the PROBE_GI lights once (the sun etc.); same as gi_runtime_frame.
     * INDIRECT GAIN (GI_BAKE_GAIN) scales the traced radiance so the baked SH is
     * bright at full precision -- doing this at BAKE time (not a runtime multiply
     * on the sparse SH) avoids banding. */
    float bake_gain = 1.0f;
    { const char *e = getenv("GI_BAKE_GAIN"); if (e) { float v = (float)atof(e); if (v > 0.0f) bake_gain = v; } }
    uint32_t nl = 0;
    const render_light_store_t *ls = scene->lights;
    if (ls != NULL) {
        for (uint32_t i = 0; i < ls->count && nl < gi->max_lights; ++i) {
            const render_light_t *L = &ls->lights[i];
            if (!(L->flags & RENDER_LIGHT_FLAG_PROBE_GI)) continue;
            gi_light_t *g = &gi->light_scratch[nl++];
            g->kind = (L->kind == RENDER_LIGHT_DIRECTIONAL) ? GI_LIGHT_DIRECTIONAL
                    : (L->kind == RENDER_LIGHT_SPOT) ? GI_LIGHT_SPOT : GI_LIGHT_POINT;
            for (int a = 0; a < 3; ++a) { g->pos[a] = L->position[a]; g->dir[a] = L->direction[a]; }
            for (int a = 0; a < 3; ++a) g->color[a] = L->color[a] * L->intensity * bake_gain;
            g->range = L->range; g->cos_inner = L->cos_inner; g->cos_outer = L->cos_outer;
        }
    }
    fprintf(stderr, "[bake] indirect gain %.1f, %u PROBE_GI light(s) in trace set\n",
            bake_gain, nl);

    /* DIRECTLY touch every chunk resident in turn (a top-down camera would miss
     * interior chunks entirely) -- for each chunk, page it + its neighbours into
     * the SDF cache, activate only the probes in its box, and trace them to
     * convergence. They keep their SH as we move to the next chunk. */
    for (int c = 0; c < s->n_chunks; ++c) {
        float cmn[3], cmx[3], cen[3], ext = 0.0f;
        for (int a = 0; a < 3; ++a) {
            cmn[a] = s->ram[c].origin[a];
            cmx[a] = cmn[a] + (float)s->ram[c].dims[a] * s->ram[c].voxel;
            cen[a] = 0.5f * (cmn[a] + cmx[a]);
            float e = cmx[a] - cmn[a]; if (e > ext) ext = e;
        }
        /* Load this chunk PLUS its immediate NEIGHBOURHOOD (chunks whose box is
         * within ~half a chunk of this one), so probe rays that leave the chunk
         * still hit real fine geometry instead of escaping early into the coarse
         * zone -- which would leak or darken the near-field indirect. The
         * resident pool caps the count; a flat sprawl's 3x3 neighbourhood fits.
         * BAKE_NBR (metres) tunes the reach. */
        static float nbr = -1.0f;
        if (nbr < 0.0f) { const char *e = getenv("BAKE_NBR"); nbr = e ? (float)atof(e) : ext * 1.1f; }
        int nvis = 0;
        for (int d = 0; d < s->n_chunks; ++d) {
            /* Box-to-box gap distance c<->d (0 if the boxes touch/overlap). */
            float bd2 = 0.0f;
            for (int a = 0; a < 3; ++a) {
                float dmn = s->ram[d].origin[a];
                float dmx = dmn + (float)s->ram[d].dims[a] * s->ram[d].voxel;
                float g = (dmn > cmx[a]) ? (dmn - cmx[a])
                        : (cmn[a] > dmx) ? (cmn[a] - dmx) : 0.0f;
                bd2 += g * g;
            }
            visible[d] = (bd2 <= nbr * nbr) ? 1 : 0;
            nvis += visible[d];
        }
        gi_sdf_stream_page(s, visible, cen);
        if (c == 0)
            fprintf(stderr, "[bake] chunk 0: %d visible, %d resident on GPU\n",
                    nvis, s->resident);

        uint32_t na = 0;
        for (uint32_t p = 0; p < np; ++p) {
            /* Trace probes ASSIGNED to this chunk (nearest) -- same criterion
             * the writer uses, so every probe traces + is written (no orphans). */
            active[p] = (bake_nearest_chunk(s, &gi->probe_pos[p * 3]) == c) ? 1 : 0;
            na += active[p];
        }
        (void)cmn; (void)cmx;
        if (na == 0) continue;
        gi_probe_gpu_set_active(&gi->gpu, active, np);

        for (int it = 0; it < iters; ++it) {
            float temporal = (it == 0) ? 1.0f : gi->smooth;   /* replace then settle. */
            gi_probe_gpu_dispatch(&gi->gpu, s, gi->light_scratch, nl, NULL, 0,
                                  gi->soft_k, temporal, 1, 0, gi->probe_grid_dim,
                                  gi->probe_grid_origin, gi->probe_grid_cell);
        }
        if ((c % 4) == 0)
            fprintf(stderr, "[bake] chunk %d/%d (%u probes)\n", c, s->n_chunks, na);
    }
    /* Restore full activity + the upload cap for any subsequent runtime use. */
    s->max_uploads = saved_cap;
    memset(active, 1, np);
    gi_probe_gpu_set_active(&gi->gpu, active, np);
    free(active); free(visible);
    fprintf(stderr, "[bake] converged %d chunks x %d iters\n", s->n_chunks, iters);
}

bool gi_runtime_bake_write_probesh(const gi_runtime_t *gi, const char *prefix)
{
    if (gi == NULL || prefix == NULL || gi->sdf_ptr == NULL || gi->probes.count == 0)
        return false;
    uint32_t np = gi->probes.count;
    const gi_sdf_stream_t *s = gi->sdf_ptr;
    /* Read the converged irradiance for every probe. */
    float *sh = malloc((size_t)np * 24 * sizeof(float));
    float *sg = malloc((size_t)np * 24 * sizeof(float));
    /* Per-chunk scratch: indices + gathered SH/SG for probes in the chunk. */
    uint32_t *cidx = malloc((size_t)np * sizeof(uint32_t));
    float *csh = malloc((size_t)np * 24 * sizeof(float));
    float *csg = malloc((size_t)np * 24 * sizeof(float));
    (void)0;
    if (!sh || !sg || !cidx || !csh || !csg) {
        free(sh); free(sg); free(cidx); free(csh); free(csg);
        return false;
    }
    gi_probe_gpu_readback(&gi->gpu, sh, sg);

    /* Assign every probe to its NEAREST chunk (same criterion as the trace) so
     * probes in padding / gaps are still written + will stream in -- no orphans. */
    int *probe_chunk = malloc((size_t)np * sizeof(int));
    if (probe_chunk == NULL) {
        free(sh); free(sg); free(cidx); free(csh); free(csg);
        return false;
    }
    for (uint32_t p = 0; p < np; ++p)
        probe_chunk[p] = bake_nearest_chunk(s, &gi->probe_pos[p * 3]);

    uint32_t total_written = 0;
    int chunks_written = 0;
    for (int c = 0; c < s->n_chunks; ++c) {
        uint32_t nc = 0;
        for (uint32_t p = 0; p < np; ++p) {
            if (probe_chunk[p] != c) continue;
            cidx[nc] = p;
            memcpy(&csh[(size_t)nc * 24], &sh[(size_t)p * 24], 24 * sizeof(float));
            memcpy(&csg[(size_t)nc * 24], &sg[(size_t)p * 24], 24 * sizeof(float));
            ++nc;
        }
        if (nc == 0) continue;
        char path[600];
        snprintf(path, sizeof path, "%s_c%03u.probesh", prefix, (unsigned)s->scan_cc[c]);
        if (probe_sh_chunk_save(path, nc, cidx, csh, csg)) {
            total_written += nc; ++chunks_written;
        }
    }
    free(probe_chunk);
    fprintf(stderr, "gi_runtime: baked %u/%u probes across %d chunks\n",
            total_written, np, chunks_written);
    free(sh); free(sg); free(cidx); free(csh); free(csg);
    return chunks_written > 0;
}

void gi_runtime_readback(const gi_runtime_t *gi, float *sh, float *sg)
{
    if (gi == NULL) return;
    gi_probe_gpu_readback(&gi->gpu, sh, sg);
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
