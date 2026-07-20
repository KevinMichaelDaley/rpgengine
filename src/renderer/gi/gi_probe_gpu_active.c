/**
 * @file gi_probe_gpu_active.c
 * @brief Per-probe ACTIVE mask for probe streaming (rpg-zygg/3c6g).
 *
 * The forward+ addresses probes POSITIONALLY through the trilinear grid
 * (probe = (z*dimY + y)*dimX + x), so a streamed probe set must never be compacted
 * -- dropping one probe would shift every later index and the shader would read
 * unrelated probes (garbage specular lobes, lattice artifacts). Instead the grid
 * stays dense and residency is expressed as an ACTIVE flag in ppos.w: inactive
 * probes remain addressable (keeping their last coefficients) but the update skips
 * them, so streaming still bounds the per-frame trace work.
 *
 * The packed positions are mirrored in a CPU shadow copy so flipping the mask is a
 * plain upload -- never a glGetBufferSubData read-back, which would stall the GPU
 * every time residency moved.
 */
#include <glad/glad.h>

#include "ferrum/renderer/gi/gi_probe_gpu.h"

#define GI_GL_SHADER_STORAGE_BUFFER 0x90D2

void gi_probe_gpu_set_active(gi_probe_gpu_t *g, const unsigned char *active, uint32_t n)
{
    if (g == NULL || !g->ready || g->n_probes == 0 || g->pos_shadow == NULL) return;
    uint32_t count = g->n_probes < g->pos_cap ? g->n_probes : g->pos_cap;
    if (count == 0) return;

    int changed = 0;
    for (uint32_t i = 0; i < count; ++i) {
        float want = (active == NULL || (i < n && active[i])) ? 1.0f : 0.0f;
        if (g->pos_shadow[i * 4u + 3u] != want) {
            g->pos_shadow[i * 4u + 3u] = want;
            changed = 1;
        }
    }
    if (!changed) return;   /* residency unchanged -> no upload at all. */

    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_pos);
    glBufferSubData(GI_GL_SHADER_STORAGE_BUFFER, 0,
                    (GLsizeiptr)count * 4 * sizeof(float), g->pos_shadow);
}
