/**
 * @file gi_brick_gpu.c
 * @brief Upload/teardown of the brick sampling GL objects (see gi_brick_gpu.h).
 */
#include <glad/glad.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/gi/gi_brick_gpu.h"

bool gi_brick_gpu_create(gi_brick_gpu_t *g, const probe_brick_data_t *bd,
                         const probe_brick_index_t *ix)
{
    if (g == NULL || bd == NULL || ix == NULL || bd->n_bricks == 0 ||
        bd->bricks == NULL || ix->brick_of == NULL)
        return false;
    memset(g, 0, sizeof *g);

    /* Voxel -> brick id: an integer 3D texture (NEAREST, clamped). */
    glGenTextures(1, &g->index_tex);
    glBindTexture(GL_TEXTURE_3D, g->index_tex);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32I, ix->dim[0], ix->dim[1], ix->dim[2],
                 0, GL_RED_INTEGER, GL_INT, ix->brick_of);

    /* Brick meta: one RGBA32F texel per brick (min.xyz, size). */
    {
        float *meta = malloc((size_t)bd->n_bricks * 4u * sizeof(float));
        if (meta == NULL) { gi_brick_gpu_destroy(g); return false; }
        for (uint32_t b = 0; b < bd->n_bricks; ++b) {
            meta[b * 4 + 0] = bd->bricks[b].min[0];
            meta[b * 4 + 1] = bd->bricks[b].min[1];
            meta[b * 4 + 2] = bd->bricks[b].min[2];
            meta[b * 4 + 3] = bd->bricks[b].size;
        }
        glGenBuffers(1, &g->meta_buf);
        glBindBuffer(GL_TEXTURE_BUFFER, g->meta_buf);
        glBufferData(GL_TEXTURE_BUFFER,
                     (GLsizeiptr)((size_t)bd->n_bricks * 4u * sizeof(float)),
                     meta, GL_STATIC_DRAW);
        free(meta);
        glGenTextures(1, &g->meta_tex);
        glBindTexture(GL_TEXTURE_BUFFER, g->meta_tex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, g->meta_buf);
    }

    /* Probe-id tables: 64 uints per brick, packed brick-major (matches the
     * probe_brick_t layout exactly, so upload straight from the structs). */
    {
        uint32_t *ids = malloc((size_t)bd->n_bricks * 64u * sizeof(uint32_t));
        if (ids == NULL) { gi_brick_gpu_destroy(g); return false; }
        for (uint32_t b = 0; b < bd->n_bricks; ++b)
            memcpy(&ids[(size_t)b * 64u], bd->bricks[b].probe_idx,
                   64u * sizeof(uint32_t));
        glGenBuffers(1, &g->pidx_buf);
        glBindBuffer(GL_TEXTURE_BUFFER, g->pidx_buf);
        glBufferData(GL_TEXTURE_BUFFER,
                     (GLsizeiptr)((size_t)bd->n_bricks * 64u * sizeof(uint32_t)),
                     ids, GL_STATIC_DRAW);
        free(ids);
        glGenTextures(1, &g->pidx_tex);
        glBindTexture(GL_TEXTURE_BUFFER, g->pidx_tex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, g->pidx_buf);
    }

    /* Per-probe validity bytes (1 usable / 0 masked). */
    {
        glGenBuffers(1, &g->valid_buf);
        glBindBuffer(GL_TEXTURE_BUFFER, g->valid_buf);
        if (bd->n_probes > 0 && bd->valid != NULL) {
            glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)bd->n_probes, bd->valid,
                         GL_STATIC_DRAW);
        } else {
            uint8_t one = 1;
            glBufferData(GL_TEXTURE_BUFFER, 1, &one, GL_STATIC_DRAW);
        }
        glGenTextures(1, &g->valid_tex);
        glBindTexture(GL_TEXTURE_BUFFER, g->valid_tex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_R8UI, g->valid_buf);
    }

    memcpy(g->dim, ix->dim, sizeof g->dim);
    memcpy(g->origin, ix->origin, sizeof g->origin);
    g->voxel = ix->voxel;
    g->on = 1;
    return true;
}

void gi_brick_gpu_destroy(gi_brick_gpu_t *g)
{
    if (g == NULL) return;
    if (g->index_tex) glDeleteTextures(1, &g->index_tex);
    if (g->meta_tex) glDeleteTextures(1, &g->meta_tex);
    if (g->pidx_tex) glDeleteTextures(1, &g->pidx_tex);
    if (g->valid_tex) glDeleteTextures(1, &g->valid_tex);
    if (g->meta_buf) glDeleteBuffers(1, &g->meta_buf);
    if (g->pidx_buf) glDeleteBuffers(1, &g->pidx_buf);
    if (g->valid_buf) glDeleteBuffers(1, &g->valid_buf);
    memset(g, 0, sizeof *g);
}
