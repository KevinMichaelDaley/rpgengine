/**
 * @file render_world_update.c
 * @brief render_world per-frame update (rpg-i3wx): GI dispatch + forward render.
 */
#include "ferrum/renderer/render_world.h"

/* Refresh the caustics bake's SDF set from the GI stream's residency
 * (rpg-39mc): the SDF is ALREADY loaded and paged by the GI runtime -- the
 * caustics trace just borrows the resident fine-chunk textures plus the
 * always-resident zone fallback. render_forward re-bakes only on change. */
static void world_feed_caustic_sdf(render_world_t *rw)
{
    const gi_sdf_stream_t *s = rw->gi.sdf_ptr;
    if (s == NULL || rw->forward.caustics.map_tex == 0u)
        return;
    uint32_t tex[GI_SDF_MAX_RESIDENT];
    float org[GI_SDF_MAX_RESIDENT][3];
    float dim[GI_SDF_MAX_RESIDENT][3];
    float vox[GI_SDF_MAX_RESIDENT];
    uint32_t n = 0;
    for (int i = 0; i < GI_SDF_MAX_RESIDENT && n < SHADOW_CAUSTICS_MAX_SDF; ++i) {
        int chunk = s->slot_chunk[i];
        if (chunk < 0 || s->tex[i] == 0u)
            continue;
        const gi_sdf_chunk_ram_t *r = &s->ram[chunk];
        tex[n] = s->tex[i];
        org[n][0] = r->origin[0]; org[n][1] = r->origin[1]; org[n][2] = r->origin[2];
        dim[n][0] = (float)r->dims[0]; dim[n][1] = (float)r->dims[1];
        dim[n][2] = (float)r->dims[2];
        vox[n] = r->voxel;
        ++n;
    }
    float zo[3] = { 0, 0, 0 }, zd[3] = { 0, 0, 0 };
    uint32_t ztex = 0u;
    float zvox = 0.0f;
    if (s->has_zone && s->zone_tex != 0u) {
        ztex = s->zone_tex;
        zo[0] = s->zone_origin[0]; zo[1] = s->zone_origin[1]; zo[2] = s->zone_origin[2];
        zd[0] = (float)s->zone_dims[0]; zd[1] = (float)s->zone_dims[1];
        zd[2] = (float)s->zone_dims[2];
        zvox = s->zone_voxel;
    }
    render_forward_set_caustic_sdf(&rw->forward, tex, org, dim, vox, n,
                                   ztex, zo, zd, zvox);
}

void render_world_update(render_world_t *rw, const gi_collider_t *boxes,
                         uint32_t n_boxes, int screen_w, int screen_h)
{
    if (rw == NULL || rw->scene == NULL) return;
    if (rw->gi_enabled) {
        gi_runtime_frame(&rw->gi, rw->scene, rw->scene->camera.view,
                         rw->scene->camera.proj, boxes, n_boxes, screen_w, screen_h);
        world_feed_caustic_sdf(rw);
        /* Streamed reflection probes ride the same chunk residency. */
        refl_stream_sync(&rw->refl, rw->gi.sdf_ptr);
    }
    render_forward_render(&rw->forward, rw->scene);
}

void render_world_set_probes(render_world_t *rw, const float *pos, uint32_t count)
{
    if (rw != NULL && rw->gi_enabled) gi_runtime_set_probes(&rw->gi, pos, count);
}

void render_world_set_visible(render_world_t *rw, const uint8_t *visible, int n_chunks)
{
    if (rw != NULL && rw->gi_enabled) gi_runtime_set_visible(&rw->gi, visible, n_chunks);
}
