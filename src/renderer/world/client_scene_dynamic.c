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
}
