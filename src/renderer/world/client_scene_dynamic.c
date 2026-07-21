/**
 * @file client_scene_dynamic.c
 * @brief Voxelise the scene's DYNAMIC objects into the probe GI's sparse dynamic
 *        albedo volume (rpg-3c6g), so their real colour bleeds into the indirect.
 *
 * Dynamic geometry is deliberately outside the offline bake: it has no lightmap
 * slot and is absent from the baked voxel albedo. That means a probe ray hitting
 * one would only resolve its distance (occlusion) and bounce the neutral grey
 * fallback -- a red cloth banner would bleed grey. Each frame this clears the
 * volume and rasterises those objects' actual triangles into it, tinted by their
 * material albedo.
 */
#include <glad/glad.h>

#include <stdio.h>
#include <stdlib.h>

#include "ferrum/renderer/client_scene.h"

void client_scene_voxelize_dynamic(client_scene_t *cs)
{
    if (cs == NULL || !cs->vox_ready || cs->dyn_count == 0) return;
    gi_probe_gpu_t *g = &cs->world.gi.gpu;

    /* Size + clear the volume over the probe play volume, then fill it. */
    int dim[3]; float extent[3];
    unsigned int tex = gi_probe_gpu_dyn_volume(g, cs->world.gi.gi_aabb_min,
                                               cs->world.gi.gi_aabb_max,
                                               0.35f, dim, extent);
    if (tex == 0u) { gi_probe_gpu_dyn_enable(g, 0); return; }

    gi_voxelize_begin(&cs->vox, tex, dim, cs->world.gi.gi_aabb_min, extent);
    for (uint32_t k = 0; k < cs->dyn_count; ++k) {
        uint32_t ri = cs->dyn_idx[k];
        if (ri >= cs->scene.count) continue;
        const render_renderable_t *r = &cs->scene.items[ri];
        if (r->mesh == NULL) continue;
        gi_voxelize_mesh(&cs->vox, r->mesh, r->model, &cs->dyn_albedo[k * 3u]);

        /* Occlusion proxy: the mesh's own bounds transformed to world, so a dynamic
         * object BLOCKS probe rays as well as colouring them. Derived from the mesh
         * (a thin mesh -> a thin box), never authored. */
        float wmin[3] = { 1e30f, 1e30f, 1e30f }, wmax[3] = { -1e30f, -1e30f, -1e30f };
        for (int c = 0; c < 8; ++c) {
            float lp[3] = { (c & 1) ? r->mesh->aabb_max[0] : r->mesh->aabb_min[0],
                            (c & 2) ? r->mesh->aabb_max[1] : r->mesh->aabb_min[1],
                            (c & 4) ? r->mesh->aabb_max[2] : r->mesh->aabb_min[2] };
            const float *m = r->model;
            float wp[3] = { m[0]*lp[0] + m[4]*lp[1] + m[8]*lp[2]  + m[12],
                            m[1]*lp[0] + m[5]*lp[1] + m[9]*lp[2]  + m[13],
                            m[2]*lp[0] + m[6]*lp[1] + m[10]*lp[2] + m[14] };
            for (int a = 0; a < 3; ++a) {
                if (wp[a] < wmin[a]) wmin[a] = wp[a];
                if (wp[a] > wmax[a]) wmax[a] = wp[a];
            }
        }
        gi_collider_t *col = &cs->dyn_col[k];
        col->kind = GI_COLLIDER_BOX;
        for (int a = 0; a < 3; ++a) {
            col->a[a] = 0.5f * (wmin[a] + wmax[a]);
            float h = 0.5f * (wmax[a] - wmin[a]);
            col->ext[a] = h > 0.01f ? h : 0.01f;   /* keep a thin plane thin. */
        }
    }
    gi_voxelize_end(&cs->vox);
    gi_probe_gpu_dyn_enable(g, 1);

    /* FR_DUMP_DYNVOL=1: readback at frame ~1 AND ~200 (post-convergence) --
     * (a) where the banner actually voxelises in world coords, (b) which probes
     * carry red-dominant SH and where they sit, (c) the GI light set. */
    {
        static int calls = 0;
        ++calls;
        if ((calls == 1 || calls == 200) && getenv("FR_DUMP_DYNVOL") != NULL) {
            int nvox = g->dyn_dim[0] * g->dyn_dim[1] * g->dyn_dim[2];
            unsigned char *px = malloc((size_t)nvox * 4u);
            if (px != NULL) {
                glBindTexture(GL_TEXTURE_3D, g->dyn_tex);
                glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
                int n_on = 0;
                for (int z = 0; z < g->dyn_dim[2]; ++z)
                    for (int y = 0; y < g->dyn_dim[1]; ++y)
                        for (int x = 0; x < g->dyn_dim[0]; ++x) {
                            const unsigned char *t =
                                &px[(((size_t)z * g->dyn_dim[1] + y) * g->dyn_dim[0] + x) * 4u];
                            if (t[3] == 0) continue;
                            ++n_on;
                            float w[3] = { g->dyn_origin[0] + ((float)x + 0.5f) * g->dyn_vox,
                                           g->dyn_origin[1] + ((float)y + 0.5f) * g->dyn_vox,
                                           g->dyn_origin[2] + ((float)z + 0.5f) * g->dyn_vox };
                            for (int a2 = 0; a2 < 3; ++a2) {
                                if (w[a2] < mn[a2]) mn[a2] = w[a2];
                                if (w[a2] > mx[a2]) mx[a2] = w[a2];
                            }
                        }
                fprintf(stderr, "[dynvol] dim %dx%dx%d vox %.3f origin (%.2f,%.2f,%.2f)\n",
                        g->dyn_dim[0], g->dyn_dim[1], g->dyn_dim[2],
                        (double)g->dyn_vox, (double)g->dyn_origin[0],
                        (double)g->dyn_origin[1], (double)g->dyn_origin[2]);
                fprintf(stderr, "[dynvol] %d voxels on, world bounds (%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f)\n",
                        n_on, (double)mn[0], (double)mn[1], (double)mn[2],
                        (double)mx[0], (double)mx[1], (double)mx[2]);
                for (uint32_t k = 0; k < cs->dyn_count; ++k) {
                    uint32_t ri = cs->dyn_idx[k];
                    if (ri >= cs->scene.count) continue;
                    const float *m = cs->scene.items[ri].model;
                    fprintf(stderr, "[dynvol] dyn[%u] renderable %u world pos (%.2f,%.2f,%.2f)\n",
                            k, ri, (double)m[12], (double)m[13], (double)m[14]);
                }
                free(px);

                /* (b) red-dominant probes: read back the SH DC terms and rank. */
                if (cs->probe_pos_full != NULL && cs->probe_count_full > 0) {
                    uint32_t n = cs->probe_count_full;
                    float *sh = malloc((size_t)n * 24u * sizeof(float));
                    if (sh != NULL) {
                        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g->b_sh);
                        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                                           (GLsizeiptr)((size_t)n * 24u * sizeof(float)), sh);
                        for (int top = 0; top < 8; ++top) {
                            float best = 0.0f; uint32_t bi = 0u;
                            for (uint32_t i = 0; i < n; ++i) {
                                float r = sh[i * 24 + 0], gg = sh[i * 24 + 4], b2 = sh[i * 24 + 8];
                                float redness = r - 0.5f * (gg + b2);
                                if (redness > best) { best = redness; bi = i; }
                            }
                            if (best <= 0.0f) break;
                            fprintf(stderr,
                                    "[redsh] probe %u pos (%.2f,%.2f,%.2f) dcRGB (%.3f,%.3f,%.3f)\n",
                                    bi, (double)cs->probe_pos_full[bi * 3 + 0],
                                    (double)cs->probe_pos_full[bi * 3 + 1],
                                    (double)cs->probe_pos_full[bi * 3 + 2],
                                    (double)sh[bi * 24 + 0], (double)sh[bi * 24 + 4],
                                    (double)sh[bi * 24 + 8]);
                            sh[bi * 24 + 0] = -1e9f;   /* exclude from next round. */
                        }
                        free(sh);
                    }
                }
                /* (c) the light set the GI can gather from. */
                if (cs->scene.lights != NULL) {
                    for (uint32_t i = 0; i < cs->scene.lights->count; ++i) {
                        const render_light_t *L = &cs->scene.lights->lights[i];
                        fprintf(stderr,
                                "[gilight] [%u] kind=%d flags=0x%x pos (%.2f,%.2f,%.2f) color (%.0f,%.0f,%.0f) x%.1f\n",
                                i, (int)L->kind, L->flags, (double)L->position[0],
                                (double)L->position[1], (double)L->position[2],
                                (double)L->color[0], (double)L->color[1],
                                (double)L->color[2], (double)L->intensity);
                    }
                }
            }
        }
    }
}
