/**
 * @file lm_svo_material.c
 * @brief Material-stamping SVO rasterization for the baker (see header).
 */
#include "ferrum/lightmap/lm_svo_material.h"

#include <math.h>

/* Triangle AABB (world space). */
static phys_aabb_t lm_svo_tri_aabb(const phys_triangle_t *tri)
{
    phys_aabb_t aabb;
    aabb.min = tri->v[0];
    aabb.max = tri->v[0];
    for (int i = 1; i < 3; ++i) {
        phys_vec3_t p = tri->v[i];
        if (p.x < aabb.min.x) aabb.min.x = p.x;
        if (p.y < aabb.min.y) aabb.min.y = p.y;
        if (p.z < aabb.min.z) aabb.min.z = p.z;
        if (p.x > aabb.max.x) aabb.max.x = p.x;
        if (p.y > aabb.max.y) aabb.max.y = p.y;
        if (p.z > aabb.max.z) aabb.max.z = p.z;
    }
    return aabb;
}

/* Inclusive integer voxel range at max depth covering @p aabb (clamped to the
 * grid). Mirrors the navigation rasteriser's per-axis normalisation so the same
 * leaves are addressed. */
static void lm_svo_voxel_range(const npc_svo_grid_t *svo, phys_aabb_t aabb,
                               uint32_t lo[3], uint32_t hi[3])
{
    uint32_t cells = 1u << svo->max_depth;
    const float mn[3] = { svo->world_bounds.min.x, svo->world_bounds.min.y,
                          svo->world_bounds.min.z };
    const float mx[3] = { svo->world_bounds.max.x, svo->world_bounds.max.y,
                          svo->world_bounds.max.z };
    const float amin[3] = { aabb.min.x, aabb.min.y, aabb.min.z };
    const float amax[3] = { aabb.max.x, aabb.max.y, aabb.max.z };
    for (int i = 0; i < 3; ++i) {
        float ext = mx[i] - mn[i];
        float a = fmaxf(amin[i], mn[i]);
        float b = fminf(amax[i], mx[i]);
        long l = (ext > 0.0f) ? (long)((a - mn[i]) / ext * (float)cells) : 0;
        long h = (ext > 0.0f) ? (long)((b - mn[i]) / ext * (float)cells) : 0;
        if (l < 0) l = 0;
        if (h < 0) h = 0;
        if (l > (long)cells - 1) l = (long)cells - 1;
        if (h > (long)cells - 1) h = (long)cells - 1;
        lo[i] = (uint32_t)l;
        hi[i] = (uint32_t)h;
    }
}

void lm_svo_stamp_triangle(npc_svo_grid_t *svo, const phys_triangle_t *tri,
                           uint16_t material_id)
{
    if (!svo || !tri)
        return;

    /* Create the solid leaves first, then stamp their material. */
    npc_svo_rasterize_triangle(svo, tri);

    uint32_t cells = 1u << svo->max_depth;
    const float mn[3] = { svo->world_bounds.min.x, svo->world_bounds.min.y,
                          svo->world_bounds.min.z };
    const float ext[3] = { svo->world_bounds.max.x - mn[0],
                           svo->world_bounds.max.y - mn[1],
                           svo->world_bounds.max.z - mn[2] };
    uint32_t lo[3], hi[3];
    lm_svo_voxel_range(svo, lm_svo_tri_aabb(tri), lo, hi);

    for (uint32_t vz = lo[2]; vz <= hi[2]; ++vz) {
        for (uint32_t vy = lo[1]; vy <= hi[1]; ++vy) {
            for (uint32_t vx = lo[0]; vx <= hi[0]; ++vx) {
                /* Voxel centre -> leaf node; stamp the material if solid. */
                phys_vec3_t centre = {
                    mn[0] + ((float)vx + 0.5f) * ext[0] / (float)cells,
                    mn[1] + ((float)vy + 0.5f) * ext[1] / (float)cells,
                    mn[2] + ((float)vz + 0.5f) * ext[2] / (float)cells
                };
                uint32_t node = NPC_SVO_INVALID_NODE;
                uint8_t flags = npc_svo_query_point(svo, centre, &node);
                if ((flags & NPC_SVO_FLAG_SOLID) && node != NPC_SVO_INVALID_NODE)
                    svo->nodes[node].material = material_id;
            }
        }
    }
}

void lm_svo_stamp_mesh(npc_svo_grid_t *svo, const phys_triangle_t *tris,
                       uint32_t count, uint16_t material_id)
{
    if (!svo || !tris)
        return;
    for (uint32_t i = 0; i < count; ++i)
        lm_svo_stamp_triangle(svo, &tris[i], material_id);
}
