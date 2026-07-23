/**
 * @file lm_gpu_voxelize_util.c
 * @brief GPU voxelizer residency helpers + shared state definitions (see
 *        lm_gpu_voxelize_internal.h, rpg-bpiz).
 */
#include "lm_gpu_voxelize_internal.h"

#include <stdlib.h>
#include <string.h>

/* Shared state (loaded/compiled by lm_gpu_voxelize_init). */
lm_voxi_gl_t lm_voxi_gl;
GLuint lm_voxi_prog, lm_voxi_clear, lm_voxi_sample, lm_voxi_fbo;
bool   lm_voxi_ready;

bool lm_voxi_mesh_overlaps(const lm_mesh_t *m, const float bmin[3],
                           const float bmax[3])
{
    if (m->vert_count == 0 || m->positions == NULL) return false;
    float mn[3] = { m->positions[0], m->positions[1], m->positions[2] };
    float mx[3] = { mn[0], mn[1], mn[2] };
    for (uint32_t v = 1; v < m->vert_count; ++v)
        for (int c = 0; c < 3; ++c) {
            float p = m->positions[v * 3 + c];
            if (p < mn[c]) mn[c] = p;
            if (p > mx[c]) mx[c] = p;
        }
    for (int c = 0; c < 3; ++c)
        if (mx[c] < bmin[c] || mn[c] > bmax[c]) return false;
    return true;
}

GLuint lm_voxi_upload_image(const lm_image_t *img)
{
    if (img == NULL || img->pixels == NULL || img->width == 0 ||
        img->height == 0 || (img->channels != 3 && img->channels != 4))
        return 0u;
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    GLuint t = 0u;
    gl->GenTextures(1, &t);
    gl->BindTexture(GLV_TEXTURE_2D, t);
    gl->PixelStorei(GLV_UNPACK_ALIGNMENT, 1);
    GLint ifmt = img->channels == 4
               ? (img->srgb ? (GLint)GLV_SRGB8_ALPHA8 : (GLint)GLV_RGBA8)
               : (img->srgb ? (GLint)GLV_SRGB8 : (GLint)GLV_RGB8);
    GLenum fmt = img->channels == 4 ? GLV_RGBA : GLV_RGB;
    gl->TexImage2D(GLV_TEXTURE_2D, 0, ifmt, (GLsizei)img->width,
                   (GLsizei)img->height, 0, fmt, GLV_UNSIGNED_BYTE,
                   img->pixels);
    gl->TexParameteri(GLV_TEXTURE_2D, GLV_TEXTURE_MIN_FILTER, GLV_LINEAR);
    gl->TexParameteri(GLV_TEXTURE_2D, GLV_TEXTURE_MAG_FILTER, GLV_LINEAR);
    gl->TexParameteri(GLV_TEXTURE_2D, GLV_TEXTURE_WRAP_S, GLV_REPEAT);
    gl->TexParameteri(GLV_TEXTURE_2D, GLV_TEXTURE_WRAP_T, GLV_REPEAT);
    gl->TexParameteri(GLV_TEXTURE_2D, GLV_TEXTURE_MAX_LEVEL, 0);
    return t;
}

bool lm_voxi_upload_mesh(const lm_mesh_t *m, lm_voxi_mesh_t *out)
{
    memset(out, 0, sizeof *out);
    out->src = m;
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    float *inter = malloc((size_t)m->vert_count * 8u * sizeof(float));
    if (inter == NULL) return false;
    out->bb_min[0] = out->bb_min[1] = out->bb_min[2] = 1e30f;
    out->bb_max[0] = out->bb_max[1] = out->bb_max[2] = -1e30f;
    for (uint32_t v = 0; v < m->vert_count; ++v) {
        float *d = &inter[v * 8u];
        for (int c = 0; c < 3; ++c) {
            float p = m->positions[v * 3 + c];
            d[c] = p;
            if (p < out->bb_min[c]) out->bb_min[c] = p;
            if (p > out->bb_max[c]) out->bb_max[c] = p;
        }
        d[3] = m->normals != NULL ? m->normals[v * 3 + 0] : 0.0f;
        d[4] = m->normals != NULL ? m->normals[v * 3 + 1] : 0.0f;
        d[5] = m->normals != NULL ? m->normals[v * 3 + 2] : 0.0f;
        d[6] = m->uv0 != NULL ? m->uv0[v * 2 + 0] : 0.0f;
        d[7] = m->uv0 != NULL ? m->uv0[v * 2 + 1] : 0.0f;
    }
    gl->GenVertexArrays(1, &out->vao);
    gl->BindVertexArray(out->vao);
    gl->GenBuffers(1, &out->vbo);
    gl->BindBuffer(GLV_ARRAY_BUFFER, out->vbo);
    gl->BufferData(GLV_ARRAY_BUFFER,
                   (GLsizeiptr)((size_t)m->vert_count * 8u * sizeof(float)),
                   inter, GLV_STATIC_DRAW);
    free(inter);
    gl->GenBuffers(1, &out->ebo);
    gl->BindBuffer(GLV_ELEMENT_ARRAY_BUFFER, out->ebo);
    gl->BufferData(GLV_ELEMENT_ARRAY_BUFFER,
                   (GLsizeiptr)((size_t)m->index_count * sizeof(uint32_t)),
                   m->indices, GLV_STATIC_DRAW);
    const GLsizei stride = 8 * (GLsizei)sizeof(float);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GLV_FLOAT, 0, stride, (const void *)0);
    gl->EnableVertexAttribArray(1);
    gl->VertexAttribPointer(1, 3, GLV_FLOAT, 0, stride,
                            (const void *)(3u * sizeof(float)));
    gl->EnableVertexAttribArray(2);
    gl->VertexAttribPointer(2, 2, GLV_FLOAT, 0, stride,
                            (const void *)(6u * sizeof(float)));
    gl->BindVertexArray(0u);
    return true;
}

void lm_voxi_free_mesh(lm_voxi_mesh_t *gm)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    if (gm->vao) gl->DeleteVertexArrays(1, &gm->vao);
    if (gm->vbo) gl->DeleteBuffers(1, &gm->vbo);
    if (gm->ebo) gl->DeleteBuffers(1, &gm->ebo);
    memset(gm, 0, sizeof *gm);
}
