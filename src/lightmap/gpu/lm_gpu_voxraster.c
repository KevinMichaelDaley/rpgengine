/**
 * @file lm_gpu_voxraster.c
 * @brief Sliced-render-target rasterization of one window (see
 *        lm_gpu_voxelize_internal.h, rpg-bpiz): for EVERY slice along the
 *        window's axis, attach the five channel layers as MRT, set the
 *        hardware clip planes to the slice's slab and draw the ENTIRE mesh
 *        set (culled by AABB against the slab). The clipper hands each slice
 *        exactly the geometry crossing it; float blending accumulates.
 */
#include "lm_gpu_voxelize_internal.h"

void lm_voxi_raster_window(const lm_voxi_mesh_t *gm, uint32_t n_gm,
                           const float origin[3], const float ext[3],
                           const int dims[3], const lm_voxi_vols_t *vols)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    const int axis = vols->axis;
    const float cell_a = ext[axis] / (float)dims[axis];

    gl->BindFramebuffer(GLV_FRAMEBUFFER, lm_voxi_fbo);
    static const GLenum bufs[LM_VOX_CHANNELS] = {
        GLV_COLOR_ATTACHMENT0 + 0, GLV_COLOR_ATTACHMENT0 + 1,
        GLV_COLOR_ATTACHMENT0 + 2, GLV_COLOR_ATTACHMENT0 + 3,
        GLV_COLOR_ATTACHMENT0 + 4
    };
    gl->Viewport(0, 0, vols->udim, vols->vdim);
    gl->Disable(GLV_DEPTH_TEST);
    gl->DepthMask(0);
    gl->Disable(GLV_CULL_FACE);
    gl->Enable(GLV_BLEND);
    gl->BlendFunc(GLV_ONE, GLV_ONE);
    for (int c = 0; c < LM_VOX_CHANNELS; ++c)
        gl->BlendEquationi((GLuint)c,
                           c == LM_VOX_CH_TRANS ? GLV_MIN : GLV_FUNC_ADD);
    gl->Enable(GLV_CLIP_DISTANCE0);
    gl->Enable(GLV_CLIP_DISTANCE1);

    gl->UseProgram(lm_voxi_prog);
    gl->Uniform3f(gl->GetUniformLocation(lm_voxi_prog, "u_origin"),
                  origin[0], origin[1], origin[2]);
    gl->Uniform3f(gl->GetUniformLocation(lm_voxi_prog, "u_extent"),
                  ext[0], ext[1], ext[2]);
    gl->Uniform1i(gl->GetUniformLocation(lm_voxi_prog, "u_axis"), axis);
    float cu = (axis == 0 ? ext[1] : ext[0]) / (float)vols->udim;
    float cv = (axis == 2 ? ext[1] : ext[2]) / (float)vols->vdim;
    gl->Uniform2f(gl->GetUniformLocation(lm_voxi_prog, "u_cell_uv"), cu, cv);
    gl->Uniform1i(gl->GetUniformLocation(lm_voxi_prog, "u_alb_map"), 0);
    gl->Uniform1i(gl->GetUniformLocation(lm_voxi_prog, "u_emi_map"), 1);
    GLint loc_c0 = gl->GetUniformLocation(lm_voxi_prog, "u_clip0");
    GLint loc_c1 = gl->GetUniformLocation(lm_voxi_prog, "u_clip1");

    static const GLfloat zero4[4] = { 0, 0, 0, 0 };
    static const GLfloat one4[4] = { 1, 1, 1, 1 };

    for (int s = 0; s < dims[axis]; ++s) {
        for (int c = 0; c < LM_VOX_CHANNELS; ++c)
            gl->FramebufferTextureLayer(GLV_FRAMEBUFFER, bufs[c],
                                        vols->tex[c], 0, s);
        gl->DrawBuffers(LM_VOX_CHANNELS, bufs);
        for (int c = 0; c < LM_VOX_CHANNELS; ++c)
            gl->ClearBufferfv(GLV_COLOR, c,
                              c == LM_VOX_CH_TRANS ? one4 : zero4);
        float slab0 = origin[axis] + (float)s * cell_a;
        float slab1 = slab0 + cell_a;
        gl->Uniform1f(loc_c0, slab0);
        gl->Uniform1f(loc_c1, slab1);
        for (uint32_t i = 0; i < n_gm; ++i) {
            const lm_mesh_t *m = gm[i].src;
            if (gm[i].bb_max[axis] < slab0 || gm[i].bb_min[axis] > slab1)
                continue;                     /* mesh never crosses the slab */
            int miss = 0;
            for (int c = 0; c < 3; ++c)
                if (gm[i].bb_max[c] < origin[c] ||
                    gm[i].bb_min[c] > origin[c] + ext[c])
                    miss = 1;
            if (miss)
                continue;
            gl->Uniform3f(gl->GetUniformLocation(lm_voxi_prog, "u_alb_tint"),
                          m->albedo.x, m->albedo.y, m->albedo.z);
            gl->Uniform3f(gl->GetUniformLocation(lm_voxi_prog, "u_emi_tint"),
                          m->emissive.x, m->emissive.y, m->emissive.z);
            float tr = 1.0f - m->opacity;
            if (tr < 0.0f) tr = 0.0f;
            if (tr > 1.0f) tr = 1.0f;
            gl->Uniform1f(gl->GetUniformLocation(lm_voxi_prog, "u_trans"), tr);
            gl->Uniform1i(gl->GetUniformLocation(lm_voxi_prog, "u_has_alb"),
                          gm[i].alb_tex != 0u);
            gl->Uniform1i(gl->GetUniformLocation(lm_voxi_prog, "u_has_emi"),
                          gm[i].emi_tex != 0u);
            gl->ActiveTexture(GLV_TEXTURE0 + 0);
            gl->BindTexture(GLV_TEXTURE_2D, gm[i].alb_tex);
            gl->ActiveTexture(GLV_TEXTURE0 + 1);
            gl->BindTexture(GLV_TEXTURE_2D, gm[i].emi_tex);
            gl->ActiveTexture(GLV_TEXTURE0);
            gl->BindVertexArray(gm[i].vao);
            gl->DrawElements(GLV_TRIANGLES, (GLsizei)m->index_count,
                             GLV_UNSIGNED_INT, NULL);
        }
    }
    gl->BindVertexArray(0u);
    gl->Disable(GLV_CLIP_DISTANCE0);
    gl->Disable(GLV_CLIP_DISTANCE1);
    gl->Disable(GLV_BLEND);
    gl->MemoryBarrier(GLV_ALL_BARRIER_BITS);
}
