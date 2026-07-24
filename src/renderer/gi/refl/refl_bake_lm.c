/**
 * @file refl_bake_lm.c
 * @brief Bake-mode lm-mesh cube renderer (see refl_bake_lm.h).
 */
#include "ferrum/renderer/gi/refl_bake_lm.h"

#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/gl_constants.h"

#define RL_LOAD(dst, name)                                                    \
    do {                                                                      \
        void *p_ = loader->get_proc_address((name), loader->user_data);       \
        if (p_ == NULL) return false;                                         \
        memcpy(&(dst), &p_, sizeof(p_));                                      \
    } while (0)

bool refl_lm_upload(refl_lm_set_t *set, const gl_loader_t *loader,
                    const lm_mesh_t *meshes, uint32_t n_meshes)
{
    if (set == NULL)
        return false;
    memset(set, 0, sizeof(*set));
    if (loader == NULL || loader->get_proc_address == NULL ||
        meshes == NULL || n_meshes == 0u)
        return false;

    void (*glGenVertexArrays)(int32_t, uint32_t *);
    void (*glGenBuffers)(int32_t, uint32_t *);
    void (*glBindBuffer)(uint32_t, uint32_t);
    void (*glBufferData)(uint32_t, ptrdiff_t, const void *, uint32_t);
    void (*glVertexAttribPointer)(uint32_t, int32_t, uint32_t, uint8_t,
                                  int32_t, const void *);
    void (*glEnableVertexAttribArray)(uint32_t);
    RL_LOAD(glGenVertexArrays, "glGenVertexArrays");
    RL_LOAD(glGenBuffers, "glGenBuffers");
    RL_LOAD(glBindBuffer, "glBindBuffer");
    RL_LOAD(glBufferData, "glBufferData");
    RL_LOAD(glVertexAttribPointer, "glVertexAttribPointer");
    RL_LOAD(glEnableVertexAttribArray, "glEnableVertexAttribArray");
    RL_LOAD(set->glBindVertexArray, "glBindVertexArray");
    RL_LOAD(set->glDeleteVertexArrays, "glDeleteVertexArrays");
    RL_LOAD(set->glDeleteBuffers, "glDeleteBuffers");
    RL_LOAD(set->glDrawElements, "glDrawElements");

    /* Count totals over the opaque meshes (glass is skipped: it would
     * rasterize as an opaque slab and hide every interior). */
    size_t total_v = 0, total_i = 0;
    for (uint32_t m = 0; m < n_meshes; ++m) {
        if (meshes[m].opacity < 0.99f)
            continue;
        total_v += meshes[m].vert_count;
        total_i += meshes[m].index_count;
    }
    if (total_v == 0u || total_i == 0u)
        return false;

    /* Bake-time staging (freed below). */
    float *verts = (float *)malloc(total_v * 6u * sizeof(float));
    uint32_t *idx = (uint32_t *)malloc(total_i * sizeof(uint32_t));
    set->index_offset = (uint32_t *)malloc(n_meshes * sizeof(uint32_t));
    set->index_count = (uint32_t *)malloc(n_meshes * sizeof(uint32_t));
    if (verts == NULL || idx == NULL || set->index_offset == NULL ||
        set->index_count == NULL) {
        free(verts);
        free(idx);
        refl_lm_destroy(set);
        return false;
    }
    size_t vo = 0, io = 0;
    for (uint32_t m = 0; m < n_meshes; ++m) {
        set->index_offset[m] = (uint32_t)io;
        set->index_count[m] = 0u;
        if (meshes[m].opacity < 0.99f)
            continue;
        const lm_mesh_t *lm = &meshes[m];
        for (uint32_t v = 0; v < lm->vert_count; ++v) {
            float *dst = &verts[(vo + v) * 6u];
            dst[0] = lm->positions[v * 3u + 0u];
            dst[1] = lm->positions[v * 3u + 1u];
            dst[2] = lm->positions[v * 3u + 2u];
            dst[3] = lm->normals[v * 3u + 0u];
            dst[4] = lm->normals[v * 3u + 1u];
            dst[5] = lm->normals[v * 3u + 2u];
        }
        for (uint32_t i = 0; i < lm->index_count; ++i)
            idx[io + i] = lm->indices[i] + (uint32_t)vo;
        set->index_count[m] = lm->index_count;
        vo += lm->vert_count;
        io += lm->index_count;
    }

    glGenVertexArrays(1, &set->vao);
    set->glBindVertexArray(set->vao);
    glGenBuffers(1, &set->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, set->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (ptrdiff_t)(total_v * 6u * sizeof(float)), verts,
                 GL_STATIC_DRAW);
    glGenBuffers(1, &set->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, set->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (ptrdiff_t)(total_i * sizeof(uint32_t)), idx,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, 0, 6 * (int32_t)sizeof(float),
                          (const void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, 0, 6 * (int32_t)sizeof(float),
                          (const void *)(3u * sizeof(float)));
    glEnableVertexAttribArray(1);
    set->glBindVertexArray(0);
    free(verts);
    free(idx);
    set->n_meshes = n_meshes;
    return true;
}

void refl_lm_draw(refl_lm_set_t *set, refl_bake_t *rb,
                  const lm_mesh_t *meshes)
{
    if (set == NULL || rb == NULL || meshes == NULL || set->vao == 0u)
        return;
    static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0,
                                     0, 0, 1, 0, 0, 0, 0, 1 };
    shader_uniform_set_mat4(&rb->cache, &rb->shader, "u_model", ident, 0);
    set->glBindVertexArray(set->vao);
    for (uint32_t m = 0; m < set->n_meshes; ++m) {
        if (set->index_count[m] == 0u)
            continue;
        float tint[3] = { meshes[m].albedo.x, meshes[m].albedo.y,
                          meshes[m].albedo.z };
        float emi[3] = { meshes[m].emissive.x, meshes[m].emissive.y,
                         meshes[m].emissive.z };
        shader_uniform_set_vec3(&rb->cache, &rb->shader, "u_tint", tint);
        shader_uniform_set_vec3(&rb->cache, &rb->shader, "u_emissive", emi);
        size_t off = (size_t)set->index_offset[m] * sizeof(uint32_t);
        set->glDrawElements(GL_TRIANGLES, (int32_t)set->index_count[m],
                            GL_UNSIGNED_INT, (const void *)off);
    }
    set->glBindVertexArray(0);
}

void refl_lm_destroy(refl_lm_set_t *set)
{
    if (set == NULL)
        return;
    if (set->glDeleteVertexArrays && set->vao)
        set->glDeleteVertexArrays(1, &set->vao);
    if (set->glDeleteBuffers && set->vbo)
        set->glDeleteBuffers(1, &set->vbo);
    if (set->glDeleteBuffers && set->ibo)
        set->glDeleteBuffers(1, &set->ibo);
    free(set->index_offset);
    free(set->index_count);
    set->vao = set->vbo = set->ibo = 0u;
    set->index_offset = set->index_count = NULL;
    set->n_meshes = 0u;
}
