/**
 * @file gi_voxelize_draw.c
 * @brief The dynamic-geometry voxelisation pass itself (see gi_voxelize.h).
 */
#include <glad/glad.h>
#include <string.h>

#include "ferrum/renderer/gi/gi_voxelize.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/texture.h"

#define GV_ALBEDO_UNIT 1   /* albedo sampler unit (the image is binding 0). */

#define GV_GL_WRITE_ONLY                      0x88B9
#define GV_GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#define GV_GL_FRAMEBUFFER_DEFAULT_WIDTH       0x9310
#define GV_GL_FRAMEBUFFER_DEFAULT_HEIGHT      0x9311

/* Saved GL state so the pass can restore the caller's framebuffer/viewport. */
static int s_vp[4];
static unsigned char s_depth_was_on;

void gi_voxelize_begin(gi_voxelize_t *v, unsigned int vol_tex, const int dim[3],
                       const float origin[3], const float extent[3])
{
    if (v == NULL || !v->ready || vol_tex == 0u || dim == NULL) return;
    for (int a = 0; a < 3; ++a) v->dim[a] = dim[a];

    glGetIntegerv(GL_VIEWPORT, s_vp);
    s_depth_was_on = (unsigned char)glIsEnabled(GL_DEPTH_TEST);

    v->BindFramebuffer(GL_FRAMEBUFFER, v->fbo);
    /* Raster resolution: the largest cross-section, so no voxel column is missed. */
    int w = dim[0] > dim[1] ? dim[0] : dim[1];
    if (dim[2] > w) w = dim[2];
    if (w < 1) w = 1;
    v->FramebufferParameteri(GL_FRAMEBUFFER, GV_GL_FRAMEBUFFER_DEFAULT_WIDTH, w);
    v->FramebufferParameteri(GL_FRAMEBUFFER, GV_GL_FRAMEBUFFER_DEFAULT_HEIGHT, w);
    glViewport(0, 0, w, w);

    /* Every surface along a ray must land in the volume, not just the nearest, and
     * there is no colour attachment to write. */
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    glUseProgram(v->prog);
    v->BindImageTexture(0, vol_tex, 0, /*layered=*/1, 0, GV_GL_WRITE_ONLY, GL_RGBA8);
    glUniform1i(v->loc_vol, 0);
    glUniform1i(v->loc_albmap, GV_ALBEDO_UNIT);   /* albedo sampler on its own unit. */
    glUniform3fv(v->loc_origin, 1, origin);
    glUniform3fv(v->loc_extent, 1, extent);
    glUniform3i(v->loc_dim, dim[0], dim[1], dim[2]);
}

void gi_voxelize_mesh(gi_voxelize_t *v, const static_mesh_t *mesh,
                      const float model[16], const render_material_t *mat)
{
    if (v == NULL || !v->ready || mesh == NULL || model == NULL) return;
    glUniformMatrix4fv(v->loc_model, 1, GL_FALSE, model);

    /* The object's REAL albedo: its material albedo map (x tint) sampled at the
     * fragment UV. No map (or no material) -> the tint alone (white if NULL). */
    const texture_t *alb = (mat != NULL) ? mat->maps[MATERIAL_TEX_ALBEDO] : NULL;
    int has_albedo = (alb != NULL && alb->handle != 0u) ? 1 : 0;
    const float white[3] = { 1.0f, 1.0f, 1.0f };
    const float unit_uv[2] = { 1.0f, 1.0f };
    glActiveTexture(GL_TEXTURE0 + GV_ALBEDO_UNIT);
    glBindTexture(GL_TEXTURE_2D, has_albedo ? alb->handle : 0u);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(v->loc_has_albedo, has_albedo);
    glUniform3fv(v->loc_tint, 1, (mat != NULL) ? mat->tint : white);
    glUniform2fv(v->loc_uvscale, 1, (mat != NULL) ? mat->uv_scale : unit_uv);
    /* Rasterise along all three axes: a surface edge-on to one projection (and so
     * covered by only a sliver of fragments there) is face-on to another, which is
     * what makes the voxelisation hole-free for arbitrary orientations. */
    for (int axis = 0; axis < 3; ++axis) {
        glUniform1i(v->loc_axis, axis);
        static_mesh_bind(mesh);
        for (uint32_t sm = 0; sm < mesh->submesh_count; ++sm)
            static_mesh_draw_submesh(mesh, sm);
    }
}

void gi_voxelize_end(gi_voxelize_t *v)
{
    if (v == NULL || !v->ready) return;
    /* Publish the image writes to the probe compute that samples the volume. */
    v->MemoryBarrier(GV_GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    glUseProgram(0);
    v->BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(s_vp[0], s_vp[1], s_vp[2], s_vp[3]);
    glDepthMask(GL_TRUE);
    if (s_depth_was_on) glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}
