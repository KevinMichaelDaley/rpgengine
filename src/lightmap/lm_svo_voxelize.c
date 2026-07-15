/**
 * @file lm_svo_voxelize.c
 * @brief Texture-sampled surface voxelization (see lm_svo_voxelize.h).
 *
 * Material voxelization is done by SUBSAMPLING each triangle's surface on a
 * uniform (isotropic) grid finer than a voxel, NOT by iterating the voxels of a
 * triangle's AABB. Triangles can be huge (a whole wall is two triangles), so a
 * single closest-point sample per voxel would smear one texel across the voxel;
 * subsampling the surface instead reads the material texture-chart at true
 * surface density. Each subsample is treated as the centre of a small disk of
 * constant material whose radius is half the sample spacing, so it represents an
 * area dA = pi * (step/2)^2. Per solid voxel we accumulate, area-weighted:
 *   - diffuse  (a bounded reflectance): area-weighted MEAN  (sum(alb*dA)/sum(dA))
 *   - emissive (a light source):        area-weighted SUM   (sum(E*dA)/voxel_area)
 *     -- two emitters in a voxel ADD (never averaged), and non-emissive geometry
 *     sharing the voxel never dilutes the emission.
 *   - normal (smooth surface normal):   area-weighted mean, renormalised.
 * Occlusion (the SOLID flags) is voxelized separately by the tight rasterizer.
 */
#include "ferrum/lightmap/lm_svo_voxelize.h"

#include <math.h>
#include <stdio.h>

#include "ferrum/lightmap/lm_image.h"
#include "ferrum/lightmap/lm_svo_mip.h"

#ifndef LM_PI
#define LM_PI 3.14159265358979324f
#endif

/* Deposit one surface subsample (barycentric wb,wc about vertex a) carrying an
 * area @p dA into the solid voxel that contains it. */
static void lm_vox_deposit(npc_svo_grid_t *svo, const lm_mesh_t *m,
                           uint32_t i0, uint32_t i1, uint32_t i2,
                           const float a[3], const float e1[3], const float e2[3],
                           float wb, float wc, float dA,
                           float *area, vec3_t *normal)
{
    float wa = 1.0f - wb - wc;
    float p[3] = { a[0] + wb*e1[0] + wc*e2[0],
                   a[1] + wb*e1[1] + wc*e2[1],
                   a[2] + wb*e1[2] + wc*e2[2] };
    uint32_t node = NPC_SVO_INVALID_NODE;
    uint8_t flags = npc_svo_query_point(svo, (phys_vec3_t){ p[0], p[1], p[2] }, &node);
    if (!(flags & NPC_SVO_FLAG_SOLID) || node == NPC_SVO_INVALID_NODE)
        return;

    /* Material from the albedo/emissive textures at the barycentric UV. */
    vec3_t alb = { 1.0f, 1.0f, 1.0f }, emi = { 1.0f, 1.0f, 1.0f };
    if (m->uv0 != NULL) {
        float mu = wa*m->uv0[i0*2]   + wb*m->uv0[i1*2]   + wc*m->uv0[i2*2];
        float mv = wa*m->uv0[i0*2+1] + wb*m->uv0[i1*2+1] + wc*m->uv0[i2*2+1];
        if (m->albedo_image)   alb = lm_image_sample(m->albedo_image, mu, mv);
        if (m->emissive_image) emi = lm_image_sample(m->emissive_image, mu, mv);
    }
    npc_svo_node_t *nd = &svo->nodes[node];
    nd->diffuse[0]  += alb.x*m->albedo.x*dA;   nd->diffuse[1]  += alb.y*m->albedo.y*dA;
    nd->diffuse[2]  += alb.z*m->albedo.z*dA;
    nd->emissive[0] += emi.x*m->emissive.x*dA; nd->emissive[1] += emi.y*m->emissive.y*dA;
    nd->emissive[2] += emi.z*m->emissive.z*dA;
    if (m->normals != NULL) {
        normal[node].x += (wa*m->normals[i0*3]   + wb*m->normals[i1*3]   + wc*m->normals[i2*3])*dA;
        normal[node].y += (wa*m->normals[i0*3+1] + wb*m->normals[i1*3+1] + wc*m->normals[i2*3+1])*dA;
        normal[node].z += (wa*m->normals[i0*3+2] + wb*m->normals[i1*3+2] + wc*m->normals[i2*3+2])*dA;
    }
    area[node] += dA;
}

/* Subsample one triangle's surface on a uniform grid of spacing @p step and
 * scatter its material (area-weighted) into the solid voxels it covers. */
static void lm_vox_triangle(npc_svo_grid_t *svo, const lm_mesh_t *m,
                            uint32_t i0, uint32_t i1, uint32_t i2,
                            float step, float *area, vec3_t *normal)
{
    const float *a = &m->positions[i0*3], *b = &m->positions[i1*3],
                *c = &m->positions[i2*3];
    float e1[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float e2[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
    /* In-plane orthonormal basis (u along e1, v = n x u). */
    float el1 = sqrtf(e1[0]*e1[0]+e1[1]*e1[1]+e1[2]*e1[2]);
    if (el1 < 1e-12f) return; /* degenerate edge -> zero area, no material */
    float u[3] = { e1[0]/el1, e1[1]/el1, e1[2]/el1 };
    float nrm[3] = { e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2],
                     e1[0]*e2[1]-e1[1]*e2[0] };
    float nl = sqrtf(nrm[0]*nrm[0]+nrm[1]*nrm[1]+nrm[2]*nrm[2]);
    if (nl < 1e-14f) return; /* degenerate (zero-area) triangle */
    nrm[0]/=nl; nrm[1]/=nl; nrm[2]/=nl;
    float v[3] = { nrm[1]*u[2]-nrm[2]*u[1], nrm[2]*u[0]-nrm[0]*u[2],
                   nrm[0]*u[1]-nrm[1]*u[0] };
    /* Triangle in the 2D plane: A=(0,0), B=(bx,0), C=(cx,cy). */
    float bx = el1;
    float cx = e2[0]*u[0]+e2[1]*u[1]+e2[2]*u[2];
    float cy = e2[0]*v[0]+e2[1]*v[1]+e2[2]*v[2];
    float det = bx*cy; /* = bx*cy - cx*0 */
    if (fabsf(det) < 1e-20f) return;
    float inv_det = 1.0f/det;

    /* Each subsample is the centre of a disk of radius step/2. */
    float dA = LM_PI * (step*0.5f) * (step*0.5f);
    float ylo = cy < 0.0f ? cy : 0.0f, yhi = cy > 0.0f ? cy : 0.0f;
    float xlo = 0.0f, xhi = bx;
    if (cx < xlo) xlo = cx; if (cx > xhi) xhi = cx;
    /* Sample grid cell centres at spacing @p step across the 2D bbox. */
    for (float py = ylo + step*0.5f; py <= yhi; py += step) {
        for (float px = xlo + step*0.5f; px <= xhi; px += step) {
            float wb = (px*cy - cx*py) * inv_det;
            float wc = (bx*py) * inv_det;
            float wa = 1.0f - wb - wc;
            if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f)
                continue; /* outside the triangle */
            lm_vox_deposit(svo, m, i0, i1, i2, a, e1, e2, wb, wc, dA, area, normal);
        }
    }
}

void lm_svo_voxelize(npc_svo_grid_t *svo, const lm_mesh_t *meshes,
                     uint32_t n_meshes, float *area, vec3_t *normal)
{
    if (svo == NULL || svo->nodes == NULL || svo->node_count == 0 ||
        meshes == NULL || area == NULL || normal == NULL)
        return;
    for (uint32_t i = 0; i < svo->node_count; ++i) {
        npc_svo_node_t *nd = &svo->nodes[i];
        nd->diffuse[0] = nd->diffuse[1] = nd->diffuse[2] = 0.0f;
        nd->emissive[0] = nd->emissive[1] = nd->emissive[2] = 0.0f;
        normal[i] = (vec3_t){ 0.0f, 0.0f, 0.0f };
        area[i] = 0.0f;
    }

    /* Voxel cell sizes (anisotropic). Subsample finer than the smallest axis so
     * every solid voxel the surface crosses gets samples; normalise emissive by
     * the orientation-averaged voxel cross-section. */
    uint32_t cells = 1u << svo->max_depth;
    float ext[3] = { svo->world_bounds.max.x - svo->world_bounds.min.x,
                     svo->world_bounds.max.y - svo->world_bounds.min.y,
                     svo->world_bounds.max.z - svo->world_bounds.min.z };
    float cs[3] = { ext[0]/(float)cells, ext[1]/(float)cells, ext[2]/(float)cells };
    float min_cs = cs[0];
    if (cs[1] < min_cs) min_cs = cs[1];
    if (cs[2] < min_cs) min_cs = cs[2];
    /* One sample per thinnest voxel edge: enough to give every solid surface
     * voxel a material sample (material varies slowly across a voxel) without
     * absurdly oversampling -- a big flat triangle then yields ~1-2 samples per
     * voxel it crosses, not thousands. */
    float step = min_cs;
    float voxel_area = (cs[0]*cs[1] + cs[1]*cs[2] + cs[0]*cs[2]) / 3.0f;
    float inv_voxel_area = (voxel_area > 0.0f) ? 1.0f/voxel_area : 0.0f;

    for (uint32_t mi = 0; mi < n_meshes; ++mi) {
        const lm_mesh_t *m = &meshes[mi];
        for (uint32_t t = 0; t + 2 < m->index_count; t += 3)
            lm_vox_triangle(svo, m, m->indices[t], m->indices[t+1],
                            m->indices[t+2], step, area, normal);
    }

    uint32_t solid_leaves = 0, solid_no_mat = 0;
    for (uint32_t i = 0; i < svo->node_count; ++i) {
        npc_svo_node_t *nd = &svo->nodes[i];
        if (nd->occupancy == 0 && (nd->flags & NPC_SVO_FLAG_SOLID)) {
            ++solid_leaves;
            if (area[i] <= 0.0f) ++solid_no_mat;
        }
        /* diffuse: area-weighted MEAN reflectance (the disk area cancels). */
        if (area[i] > 0.0f) {
            float inv = 1.0f / area[i];
            for (int k = 0; k < 3; ++k) nd->diffuse[k] *= inv;
        }
        /* emissive: area-weighted SUM of emitted radiance over the voxel cross-
         * section -- emitters add, and it is independent of tessellation. */
        for (int k = 0; k < 3; ++k) nd->emissive[k] *= inv_voxel_area;
        /* Smooth per-voxel surface normal (renormalise the area-weighted sum). */
        float *nn = &normal[i].x;
        float len = sqrtf(nn[0]*nn[0] + nn[1]*nn[1] + nn[2]*nn[2]);
        if (len > 1e-8f) { nn[0]/=len; nn[1]/=len; nn[2]/=len; }
    }
    fprintf(stderr, "voxelize: %u solid leaves, %u with NO material (%.1f%% gap)\n",
            solid_leaves, solid_no_mat,
            solid_leaves ? 100.0f*(float)solid_no_mat/(float)solid_leaves : 0.0f);
    lm_svo_mip_average_up(svo);
}
