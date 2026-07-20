/**
 * @file gi_probe_gpu_dyn.c
 * @brief The sparse DYNAMIC albedo volume owned by the probe update (rpg-3c6g):
 *        size it over the GI AABB, (re)create + clear it, and flag whether it has
 *        content. The volume is FILLED by rasterising the dynamic meshes into it
 *        (gi_voxelize) -- dynamic geometry is absent from the baked voxel albedo,
 *        so without this the probes see those objects as occluders only and bounce
 *        a neutral grey instead of their real colour.
 */
#include <glad/glad.h>
#include <string.h>

#include "ferrum/renderer/gi/gi_probe_gpu.h"

/* Voxel budget: LOW-RES (probe-scale) on purpose -- the volume only has to say
 * which dynamic object's colour is roughly where, not resolve its silhouette. */
#define GI_DYN_MAX_DIM 48

unsigned int gi_probe_gpu_dyn_volume(gi_probe_gpu_t *g, const float aabb_min[3],
                                     const float aabb_max[3], float vox,
                                     int out_dim[3], float out_extent[3])
{
    if (g == NULL || !g->ready || aabb_min == NULL || aabb_max == NULL) return 0u;
    if (vox <= 0.0f) vox = 0.5f;

    /* Size to the AABB, growing the voxel if that would blow the budget. */
    int dim[3];
    for (int a = 0; a < 3; ++a) {
        float ext = aabb_max[a] - aabb_min[a];
        if (ext < vox) ext = vox;
        int d = (int)(ext / vox) + 1;
        if (d > GI_DYN_MAX_DIM) vox = ext / (float)(GI_DYN_MAX_DIM - 1);
    }
    for (int a = 0; a < 3; ++a) {
        float ext = aabb_max[a] - aabb_min[a];
        if (ext < vox) ext = vox;
        int d = (int)(ext / vox) + 1;
        dim[a] = d < 1 ? 1 : (d > GI_DYN_MAX_DIM ? GI_DYN_MAX_DIM : d);
    }

    int resized = (dim[0] != g->dyn_dim[0] || dim[1] != g->dyn_dim[1] ||
                   dim[2] != g->dyn_dim[2] || g->dyn_tex == 0u);
    if (g->dyn_tex == 0u) {
        glGenTextures(1, &g->dyn_tex);
        glBindTexture(GL_TEXTURE_3D, g->dyn_tex);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_3D, g->dyn_tex);
    if (resized)
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, dim[0], dim[1], dim[2], 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    /* Clear to a=0 ("no dynamic geometry") so last frame's voxels never linger.
     * glClearTexImage is 4.4, so zero-upload from a static scratch (the volume is
     * tiny by construction: <= 48^3 RGBA8). */
    {
        static unsigned char s_zero[GI_DYN_MAX_DIM * GI_DYN_MAX_DIM * GI_DYN_MAX_DIM * 4];
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, dim[0], dim[1], dim[2],
                        GL_RGBA, GL_UNSIGNED_BYTE, s_zero);
    }

    for (int a = 0; a < 3; ++a) {
        g->dyn_dim[a] = dim[a];
        g->dyn_origin[a] = aabb_min[a];
        if (out_dim) out_dim[a] = dim[a];
        if (out_extent) out_extent[a] = (float)dim[a] * vox;
    }
    g->dyn_vox = vox;
    return g->dyn_tex;
}

void gi_probe_gpu_dyn_enable(gi_probe_gpu_t *g, int on)
{
    if (g != NULL) g->dyn_on = on ? 1 : 0;
}
