/**
 * @file lm_svo_voxelize.c
 * @brief Texture-sampled surface voxelization (see lm_svo_voxelize.h).
 */
#include "ferrum/lightmap/lm_svo_voxelize.h"

#include <math.h>
#include <string.h>

#include "ferrum/lightmap/lm_image.h"
#include "ferrum/lightmap/lm_svo_mip.h"

/* Barycentric coordinates of @p p projected onto triangle (a,b,c). Returns
 * false for a degenerate triangle. */
static int lm_vox_bary(const float *a, const float *b, const float *c,
                       const float p[3], float *wa, float *wb, float *wc)
{
    float v0[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float v1[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
    float v2[3] = { p[0]-a[0], p[1]-a[1], p[2]-a[2] };
    float d00 = v0[0]*v0[0]+v0[1]*v0[1]+v0[2]*v0[2];
    float d01 = v0[0]*v1[0]+v0[1]*v1[1]+v0[2]*v1[2];
    float d11 = v1[0]*v1[0]+v1[1]*v1[1]+v1[2]*v1[2];
    float d20 = v2[0]*v0[0]+v2[1]*v0[1]+v2[2]*v0[2];
    float d21 = v2[0]*v1[0]+v2[1]*v1[1]+v2[2]*v1[2];
    float denom = d00*d11 - d01*d01;
    if (fabsf(denom) < 1e-12f)
        return 0;
    float inv = 1.0f / denom;
    *wb = (d11*d20 - d01*d21) * inv;
    *wc = (d00*d21 - d01*d20) * inv;
    *wa = 1.0f - *wb - *wc;
    return 1;
}

/* Sample one mesh triangle's material into every solid voxel it covers. */
static void lm_vox_triangle(npc_svo_grid_t *svo, const lm_mesh_t *m,
                            uint32_t i0, uint32_t i1, uint32_t i2,
                            uint32_t *count)
{
    const float *a = &m->positions[i0*3], *b = &m->positions[i1*3],
                *c = &m->positions[i2*3];
    uint32_t cells = 1u << svo->max_depth;
    const float mn[3] = { svo->world_bounds.min.x, svo->world_bounds.min.y,
                          svo->world_bounds.min.z };
    const float ext[3] = { svo->world_bounds.max.x - mn[0],
                           svo->world_bounds.max.y - mn[1],
                           svo->world_bounds.max.z - mn[2] };
    /* Triangle-plane normal (for the perpendicular-distance cull). */
    float e1[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float e2[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
    float nrm[3] = { e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2],
                     e1[0]*e2[1]-e1[1]*e2[0] };
    float nl = sqrtf(nrm[0]*nrm[0]+nrm[1]*nrm[1]+nrm[2]*nrm[2]);
    if (nl > 1e-12f) { nrm[0]/=nl; nrm[1]/=nl; nrm[2]/=nl; }

    /* Inclusive voxel range over the triangle AABB. */
    uint32_t lo[3], hi[3];
    for (int k = 0; k < 3; ++k) {
        float amin = a[k] < b[k] ? (a[k] < c[k] ? a[k] : c[k]) : (b[k] < c[k] ? b[k] : c[k]);
        float amax = a[k] > b[k] ? (a[k] > c[k] ? a[k] : c[k]) : (b[k] > c[k] ? b[k] : c[k]);
        float e = ext[k];
        long l = (e > 0.0f) ? (long)((amin - mn[k]) / e * (float)cells) : 0;
        long h = (e > 0.0f) ? (long)((amax - mn[k]) / e * (float)cells) : 0;
        if (l < 0) l = 0;
        if (h < 0) h = 0;
        if (l > (long)cells-1) l = (long)cells-1;
        if (h > (long)cells-1) h = (long)cells-1;
        lo[k] = (uint32_t)l; hi[k] = (uint32_t)h;
    }
    /* Perpendicular cull threshold: one voxel diagonal. */
    float cs[3] = { ext[0]/(float)cells, ext[1]/(float)cells, ext[2]/(float)cells };
    float thresh = sqrtf(cs[0]*cs[0]+cs[1]*cs[1]+cs[2]*cs[2]);

    for (uint32_t vz = lo[2]; vz <= hi[2]; ++vz)
    for (uint32_t vy = lo[1]; vy <= hi[1]; ++vy)
    for (uint32_t vx = lo[0]; vx <= hi[0]; ++vx) {
        float p[3] = { mn[0] + ((float)vx+0.5f)*cs[0],
                       mn[1] + ((float)vy+0.5f)*cs[1],
                       mn[2] + ((float)vz+0.5f)*cs[2] };
        uint32_t node = NPC_SVO_INVALID_NODE;
        uint8_t flags = npc_svo_query_point(svo, (phys_vec3_t){p[0],p[1],p[2]}, &node);
        if (!(flags & NPC_SVO_FLAG_SOLID) || node == NPC_SVO_INVALID_NODE)
            continue;
        /* Voxel must lie within a voxel of the triangle plane. */
        float dperp = fabsf(nrm[0]*(p[0]-a[0])+nrm[1]*(p[1]-a[1])+nrm[2]*(p[2]-a[2]));
        if (dperp > thresh)
            continue;
        float wa, wb, wc;
        if (!lm_vox_bary(a, b, c, p, &wa, &wb, &wc))
            continue;
        if (wa < -0.05f || wb < -0.05f || wc < -0.05f)
            continue;
        /* Sample the material textures at the barycentric UV, times the tint. */
        vec3_t alb = { 1.0f, 1.0f, 1.0f }, emi = { 1.0f, 1.0f, 1.0f };
        if (m->uv0 != NULL) {
            float mu = wa*m->uv0[i0*2]   + wb*m->uv0[i1*2]   + wc*m->uv0[i2*2];
            float mv = wa*m->uv0[i0*2+1] + wb*m->uv0[i1*2+1] + wc*m->uv0[i2*2+1];
            if (m->albedo_image)   alb = lm_image_sample(m->albedo_image, mu, mv);
            if (m->emissive_image) emi = lm_image_sample(m->emissive_image, mu, mv);
        }
        npc_svo_node_t *nd = &svo->nodes[node];
        nd->diffuse[0]  += alb.x*m->albedo.x; nd->diffuse[1]  += alb.y*m->albedo.y;
        nd->diffuse[2]  += alb.z*m->albedo.z;
        nd->emissive[0] += emi.x*m->emissive.x; nd->emissive[1] += emi.y*m->emissive.y;
        nd->emissive[2] += emi.z*m->emissive.z;
        count[node] += 1u;
    }
}

void lm_svo_voxelize(npc_svo_grid_t *svo, const lm_mesh_t *meshes,
                     uint32_t n_meshes, uint32_t *count)
{
    if (svo == NULL || svo->nodes == NULL || svo->node_count == 0 ||
        meshes == NULL || count == NULL)
        return;
    for (uint32_t i = 0; i < svo->node_count; ++i) {
        npc_svo_node_t *nd = &svo->nodes[i];
        nd->diffuse[0] = nd->diffuse[1] = nd->diffuse[2] = 0.0f;
        nd->emissive[0] = nd->emissive[1] = nd->emissive[2] = 0.0f;
        count[i] = 0u;
    }
    for (uint32_t mi = 0; mi < n_meshes; ++mi) {
        const lm_mesh_t *m = &meshes[mi];
        for (uint32_t t = 0; t + 2 < m->index_count; t += 3)
            lm_vox_triangle(svo, m, m->indices[t], m->indices[t+1],
                            m->indices[t+2], count);
    }
    for (uint32_t i = 0; i < svo->node_count; ++i) {
        if (count[i] > 1u) {
            float inv = 1.0f / (float)count[i];
            npc_svo_node_t *nd = &svo->nodes[i];
            for (int k = 0; k < 3; ++k) { nd->diffuse[k] *= inv; nd->emissive[k] *= inv; }
        }
    }
    lm_svo_mip_average_up(svo);
}
