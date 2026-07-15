/**
 * @file lm_gpu_pack.c
 * @brief Host-side packing of the lightmap scene into GPU buffers (lm_gpu_pack.h).
 */
#include "ferrum/lightmap/gpu/lm_gpu_pack.h"

#include <stddef.h>

uint32_t lm_gpu_pack_nodes(const npc_svo_grid_t *svo, lm_gpu_node_t *out,
                           uint32_t out_cap)
{
    if (svo == NULL || svo->nodes == NULL)
        return 0u;
    uint32_t n = svo->node_count;
    if (out == NULL || out_cap < n)
        return n; /* report required size; write nothing. */
    for (uint32_t i = 0; i < n; ++i) {
        const npc_svo_node_t *s = &svo->nodes[i];
        lm_gpu_node_t *d = &out[i];
        for (int c = 0; c < 8; ++c) d->children[c] = s->children[c];
        d->diffuse[0] = s->diffuse[0]; d->diffuse[1] = s->diffuse[1];
        d->diffuse[2] = s->diffuse[2]; d->diffuse[3] = 0.0f;
        d->emissive[0] = s->emissive[0]; d->emissive[1] = s->emissive[1];
        d->emissive[2] = s->emissive[2]; d->emissive[3] = 0.0f;
    }
    return n;
}

uint32_t lm_gpu_pack_luxels(const lm_lightmap_t *lm, lm_gpu_luxel_t *out,
                            uint32_t out_cap)
{
    if (lm == NULL || lm->luxels == NULL)
        return 0u;
    uint32_t n = lm->res_u * lm->res_v;
    if (out == NULL || out_cap < n)
        return n;
    for (uint32_t i = 0; i < n; ++i) {
        const lm_luxel_t *s = &lm->luxels[i];
        out[i].pos[0] = s->pos.x; out[i].pos[1] = s->pos.y;
        out[i].pos[2] = s->pos.z; out[i].pos[3] = 0.0f;
        out[i].normal[0] = s->normal.x; out[i].normal[1] = s->normal.y;
        out[i].normal[2] = s->normal.z; out[i].normal[3] = 0.0f;
    }
    return n;
}

uint32_t lm_gpu_pack_lights(const lm_light_t *lights, uint32_t n,
                            lm_gpu_light_t *out, uint32_t out_cap)
{
    if (lights == NULL)
        return 0u;
    if (out == NULL || out_cap < n)
        return n;
    for (uint32_t i = 0; i < n; ++i) {
        const lm_light_t *s = &lights[i];
        out[i].position[0] = s->position.x; out[i].position[1] = s->position.y;
        out[i].position[2] = s->position.z; out[i].position[3] = (float)s->kind;
        out[i].direction[0] = s->direction.x; out[i].direction[1] = s->direction.y;
        out[i].direction[2] = s->direction.z; out[i].direction[3] = s->range;
        out[i].color[0] = s->color.x; out[i].color[1] = s->color.y;
        out[i].color[2] = s->color.z; out[i].color[3] = s->cos_inner;
        out[i].cone[0] = s->cos_outer; out[i].cone[1] = 0.0f;
        out[i].cone[2] = 0.0f; out[i].cone[3] = 0.0f;
    }
    return n;
}

void lm_gpu_pack_params(const npc_svo_grid_t *svo, uint32_t n_luxels,
                        uint32_t n_lights, float transition, float maxdist,
                        uint32_t bounces, lm_gpu_params_t *out)
{
    if (svo == NULL || out == NULL)
        return;
    out->bounds_min[0] = svo->world_bounds.min.x;
    out->bounds_min[1] = svo->world_bounds.min.y;
    out->bounds_min[2] = svo->world_bounds.min.z;
    out->bounds_min[3] = svo->voxel_size;
    out->bounds_max[0] = svo->world_bounds.max.x;
    out->bounds_max[1] = svo->world_bounds.max.y;
    out->bounds_max[2] = svo->world_bounds.max.z;
    out->bounds_max[3] = transition;
    out->misc[0] = maxdist; out->misc[1] = 0.0f; out->misc[2] = 0.0f; out->misc[3] = 0.0f;
    out->counts[0] = svo->node_count;
    out->counts[1] = n_luxels;
    out->counts[2] = n_lights;
    out->counts[3] = bounces;
}
