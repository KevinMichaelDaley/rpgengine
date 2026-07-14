/**
 * @file lm_svo_voxelize.c
 * @brief Texture-sampled surface voxelization (see lm_svo_voxelize.h).
 */
#include "ferrum/lightmap/lm_svo_voxelize.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/lightmap/lm_image.h"
#include "ferrum/lightmap/lm_svo_mip.h"

static float lm_vox_dot(const float a[3], const float b[3])
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

/* Barycentric coords of the CLOSEST point on triangle (a,b,c) to @p p (Ericson,
 * Real-Time Collision Detection): clamps to edges/vertices, so it is robust for
 * points outside the triangle and for triangles smaller than a voxel. Always
 * returns a valid point on the triangle. */
static void lm_vox_closest_bary(const float *a, const float *b, const float *c,
                                const float p[3], float *wa, float *wb, float *wc)
{
    float ab[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float ac[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
    float ap[3] = { p[0]-a[0], p[1]-a[1], p[2]-a[2] };
    float d1 = lm_vox_dot(ab, ap), d2 = lm_vox_dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) { *wa=1; *wb=0; *wc=0; return; }
    float bp[3] = { p[0]-b[0], p[1]-b[1], p[2]-b[2] };
    float d3 = lm_vox_dot(ab, bp), d4 = lm_vox_dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) { *wa=0; *wb=1; *wc=0; return; }
    float vc = d1*d4 - d3*d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1/(d1-d3); *wa=1-v; *wb=v; *wc=0; return;
    }
    float cp[3] = { p[0]-c[0], p[1]-c[1], p[2]-c[2] };
    float d5 = lm_vox_dot(ab, cp), d6 = lm_vox_dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) { *wa=0; *wb=0; *wc=1; return; }
    float vb = d5*d2 - d1*d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2/(d2-d6); *wa=1-w; *wb=0; *wc=w; return;
    }
    float va = d3*d6 - d5*d4;
    if (va <= 0.0f && (d4-d3) >= 0.0f && (d5-d6) >= 0.0f) {
        float w = (d4-d3)/((d4-d3)+(d5-d6)); *wa=0; *wb=1-w; *wc=w; return;
    }
    float denom = 1.0f/(va+vb+vc);
    float v = vb*denom, w = vc*denom; *wa=1-v-w; *wb=v; *wc=w;
}

/* Sample one mesh triangle's material + smooth normal into every solid voxel it
 * covers. @p normal accumulates the barycentric surface normal per node. */
static void lm_vox_triangle(npc_svo_grid_t *svo, const lm_mesh_t *m,
                            uint32_t i0, uint32_t i1, uint32_t i2,
                            uint32_t *count, vec3_t *normal)
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
        /* Sample at the CLOSEST point on the triangle to the voxel centre, and
         * gate on that distance (the stamp marks a voxel whenever the triangle
         * overlaps its box, so the centre may be a bit outside the triangle or a
         * triangle may be smaller than the voxel -- a strict inside-test would
         * leave half the solid voxels unshaded). */
        float wa, wb, wc;
        lm_vox_closest_bary(a, b, c, p, &wa, &wb, &wc);
        float cpz[3] = { a[0]*wa+b[0]*wb+c[0]*wc, a[1]*wa+b[1]*wb+c[1]*wc,
                         a[2]*wa+b[2]*wb+c[2]*wc };
        float dx=p[0]-cpz[0], dy=p[1]-cpz[1], dz=p[2]-cpz[2];
        float gate = 6.0f*thresh; /* cover the stamp's multi-voxel solid band */
        if (dx*dx+dy*dy+dz*dz > gate*gate)
            continue;
        (void)nrm;
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
        /* Smooth (barycentric-interpolated vertex) normal -> per-voxel normal,
         * so the gather shades this hit with the real surface normal. */
        if (m->normals != NULL) {
            normal[node].x += wa*m->normals[i0*3]   + wb*m->normals[i1*3]   + wc*m->normals[i2*3];
            normal[node].y += wa*m->normals[i0*3+1] + wb*m->normals[i1*3+1] + wc*m->normals[i2*3+1];
            normal[node].z += wa*m->normals[i0*3+2] + wb*m->normals[i1*3+2] + wc*m->normals[i2*3+2];
        }
        count[node] += 1u;
    }
}

void lm_svo_voxelize(npc_svo_grid_t *svo, const lm_mesh_t *meshes,
                     uint32_t n_meshes, uint32_t *count, vec3_t *normal)
{
    if (svo == NULL || svo->nodes == NULL || svo->node_count == 0 ||
        meshes == NULL || count == NULL || normal == NULL)
        return;
    for (uint32_t i = 0; i < svo->node_count; ++i) {
        npc_svo_node_t *nd = &svo->nodes[i];
        nd->diffuse[0] = nd->diffuse[1] = nd->diffuse[2] = 0.0f;
        nd->emissive[0] = nd->emissive[1] = nd->emissive[2] = 0.0f;
        normal[i] = (vec3_t){ 0.0f, 0.0f, 0.0f };
        count[i] = 0u;
    }
    for (uint32_t mi = 0; mi < n_meshes; ++mi) {
        const lm_mesh_t *m = &meshes[mi];
        for (uint32_t t = 0; t + 2 < m->index_count; t += 3)
            lm_vox_triangle(svo, m, m->indices[t], m->indices[t+1],
                            m->indices[t+2], count, normal);
    }
    uint32_t solid_leaves = 0, solid_no_mat = 0;
    for (uint32_t i = 0; i < svo->node_count; ++i) {
        const npc_svo_node_t *nc = &svo->nodes[i];
        if (nc->occupancy == 0 && (nc->flags & NPC_SVO_FLAG_SOLID)) {
            ++solid_leaves;
            if (count[i] == 0u) ++solid_no_mat;
        }
        if (count[i] > 1u) {
            float inv = 1.0f / (float)count[i];
            npc_svo_node_t *nd = &svo->nodes[i];
            for (int k = 0; k < 3; ++k) { nd->diffuse[k] *= inv; nd->emissive[k] *= inv; }
        }
        /* Normalise the accumulated per-voxel normal (leaves only; the gather
         * reads leaf normals on near hits). */
        float *nn = &normal[i].x;
        float len = sqrtf(nn[0]*nn[0] + nn[1]*nn[1] + nn[2]*nn[2]);
        if (len > 1e-8f) { nn[0]/=len; nn[1]/=len; nn[2]/=len; }
    }
    fprintf(stderr, "voxelize: %u solid leaves, %u with NO material (%.1f%% gap)\n",
            solid_leaves, solid_no_mat,
            solid_leaves ? 100.0f*(float)solid_no_mat/(float)solid_leaves : 0.0f);
    lm_svo_mip_average_up(svo);
}
