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

#define GI_MAX_PROBES 65536u

bool gi_runtime_init(gi_runtime_t *gi, const gi_runtime_config_t *cfg)
{
    if (gi == NULL || cfg == NULL || cfg->loader == NULL || cfg->sdf_prefix == NULL)
        return false;
    memset(gi, 0, sizeof *gi);
    gi->soft_k = cfg->soft_k > 0.0f ? cfg->soft_k : 8.0f;
    gi->update_interval = cfg->update_interval > 0 ? cfg->update_interval : 8;

    /* --- Baked SDF residency. --- */
    if (gi_sdf_stream_load(&gi->sdf, cfg->sdf_prefix) <= 0) {
        fprintf(stderr, "gi_runtime: no SDF chunks at %s\n", cfg->sdf_prefix);
        return false;
    }
    gi->n_sdf_boxes = gi_sdf_stream_boxes(&gi->sdf, gi->box_min, gi->box_max);

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
            float d = gi_sdf_stream_sample(&gi->sdf, p);
            if (d > 0.05f && d < band) {
                if (keep != i) { gi->probe_pos[keep*3+0]=p[0]; gi->probe_pos[keep*3+1]=p[1]; gi->probe_pos[keep*3+2]=p[2]; }
                ++keep;
            }
        }
        gi->probes.count = keep;
        fprintf(stderr, "gi_runtime: probes pruned %u -> %u\n", seeded, keep);
    }
    if (gi->probes.count == 0) { fprintf(stderr, "gi_runtime: 0 probes\n"); gi_runtime_destroy(gi); return false; }

    float cell = cfg->grid_cell > 0.0f ? cfg->grid_cell : (spacing * 2.0f);
    /* Grid cell count for the accel structure. */
    int gd[3];
    for (int a = 0; a < 3; ++a) {
        float span = cfg->aabb_max[a] - cfg->aabb_min[a];
        int d = (int)(span / cell); if ((float)d * cell < span) d++; gd[a] = d < 1 ? 1 : d;
    }
    uint32_t ncells = (uint32_t)gd[0] * (uint32_t)gd[1] * (uint32_t)gd[2];
    gi->cell_start = malloc((size_t)(ncells + 1) * sizeof(uint32_t));
    gi->probe_idx = malloc((size_t)gi->probes.count * sizeof(uint32_t));
    if (gi->cell_start == NULL || gi->probe_idx == NULL) { gi_runtime_destroy(gi); return false; }
    if (!gi_probe_grid_build(&gi->grid, &gi->probes, cfg->aabb_min, cfg->aabb_max,
                             cell, gi->cell_start, ncells + 1u, gi->probe_idx, gi->probes.count)) {
        fprintf(stderr, "gi_runtime: accel grid build failed\n"); gi_runtime_destroy(gi); return false;
    }

    /* --- GPU compute + probe upload. --- */
    gi->max_lights = cfg->max_lights ? cfg->max_lights : 512u;
    gi->light_scratch = malloc((size_t)gi->max_lights * sizeof(gi_light_t));
    if (gi->light_scratch == NULL) { gi_runtime_destroy(gi); return false; }
    if (!gi_probe_gpu_init(&gi->gpu, cfg->loader, gi->probes.count,
                           gi->max_lights, cfg->max_boxes ? cfg->max_boxes : 64u)) {
        gi_runtime_destroy(gi); return false;
    }
    gi_probe_gpu_set_probes(&gi->gpu, gi->probe_pos, gi->probes.count);

    /* --- Accel grid as texture buffers for the forward+ sampler. --- */
    glGenBuffers(1, &gi->tbo_cs); glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_cs);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(ncells + 1) * sizeof(uint32_t), gi->cell_start, GL_STATIC_DRAW);
    glGenTextures(1, &gi->tbo_cs_tex); glBindTexture(GL_TEXTURE_BUFFER, gi->tbo_cs_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, gi->tbo_cs);
    glGenBuffers(1, &gi->tbo_pi); glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_pi);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)gi->probes.count * sizeof(uint32_t), gi->probe_idx, GL_STATIC_DRAW);
    glGenTextures(1, &gi->tbo_pi_tex); glBindTexture(GL_TEXTURE_BUFFER, gi->tbo_pi_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, gi->tbo_pi);

    /* --- Probe froxel grid: bins probes into the SAME clusters the forward+
     * lights use (config MUST match fcfg.cluster), so the material samples probe
     * candidates from the fragment's own froxel via the forward+ cluster uniforms. */
    gi->probe_min = cfg->probe_min > 0 ? cfg->probe_min : 4u;
    gi->probe_sphere_margin = cfg->probe_sphere_margin > 0.0f ? cfg->probe_sphere_margin : 1.5f;
    gi->bin_interval = cfg->bin_interval > 0 ? cfg->bin_interval : 1;
    {
        cluster_config_t fc = cfg->froxel;
        if (fc.tiles_x == 0) fc = (cluster_config_t){ 16, 16, 24, 0.2f, 60.0f };
        uint32_t ctot = fc.tiles_x * fc.tiles_y * fc.slices;
        uint32_t per = gi->probes.count < 64u ? gi->probes.count : 64u;
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
                            gi->sdf.n_chunks) != 0) {
        gi_runtime_destroy(gi); return false;
    }
    fprintf(stderr, "gi_runtime: %u probes, %d SDF chunks, accel %dx%dx%d\n",
            gi->probes.count, gi->sdf.n_chunks, gd[0], gd[1], gd[2]);
    gi->ready = true;
    return true;
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
     * bin_interval lets it skip frames when the view barely changes. */
    if (gi->frame_counter % gi->bin_interval == 0) {
        render_camera_t cam;
        memset(&cam, 0, sizeof cam);
        for (int i = 0; i < 16; ++i) { cam.view[i] = view[i]; cam.proj[i] = proj[i]; }
        cluster_grid_build_points(&gi->froxel, &cam, gi->probe_pos,
                                  gi->probes.count, gi->probe_min,
                                  gi->probe_sphere_margin);
        uint32_t ctot = gi->froxel.cluster_total;
        uint32_t nidx = gi->froxel.index_count > 0 ? gi->froxel.index_count : 1u;
        glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_fo);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)ctot * sizeof(uint32_t), gi->fx_off);
        glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_fc);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)ctot * sizeof(uint32_t), gi->fx_cnt);
        glBindBuffer(GL_TEXTURE_BUFFER, gi->tbo_fi);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)nidx * sizeof(uint32_t), gi->fx_idx);
    }

    /* GI is low-frequency: recompute only every update_interval frames. Between
     * updates the probe SH persists in its buffer and the forward+ keeps sampling
     * it, so the indirect just lags slightly (imperceptible for slow lights). */
    if (gi->frame_counter++ % gi->update_interval != 0)
        return;
    /* Page the on-screen SDF chunks (per-fragment world-pos classification). */
    gi_vis_prepass_run_world(&gi->pp, scene, view, proj, gi->box_min, gi->box_max,
                             gi->n_sdf_boxes, main_w, main_h);
    gi_sdf_stream_page(&gi->sdf, gi->pp.visible);

    /* Gather the scene lights tagged DYNAMIC_INDIRECT into the trace's light set. */
    uint32_t n = 0;
    const render_light_store_t *ls = scene->lights;
    if (ls != NULL) {
        for (uint32_t i = 0; i < ls->count && n < gi->max_lights; ++i) {
            const render_light_t *L = &ls->lights[i];
            if (!(L->flags & RENDER_LIGHT_FLAG_DYNAMIC_INDIRECT)) continue;
            gi_light_t *g = &gi->light_scratch[n++];
            g->kind = (L->kind == RENDER_LIGHT_DIRECTIONAL) ? GI_LIGHT_DIRECTIONAL
                    : (L->kind == RENDER_LIGHT_SPOT) ? GI_LIGHT_SPOT : GI_LIGHT_POINT;
            for (int a = 0; a < 3; ++a) { g->pos[a] = L->position[a]; g->dir[a] = L->direction[a]; }
            for (int a = 0; a < 3; ++a) g->color[a] = L->color[a] * L->intensity;
            g->range = L->range; g->cos_inner = L->cos_inner; g->cos_outer = L->cos_outer;
        }
    }
    /* March every probe to every tagged light through the resident combined SDF. */
    gi_probe_gpu_dispatch(&gi->gpu, &gi->sdf, gi->light_scratch, n, boxes, n_boxes, gi->soft_k);
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

    shader_uniform_set_int(cache, program, "u_gi_enabled", 1);
}

void gi_runtime_destroy(gi_runtime_t *gi)
{
    if (gi == NULL) return;
    gi_vis_prepass_destroy(&gi->pp);
    gi_sdf_stream_destroy(&gi->sdf);
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
    free(gi->light_scratch);
    memset(gi, 0, sizeof *gi);
}
