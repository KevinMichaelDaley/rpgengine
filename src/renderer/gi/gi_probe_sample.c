/**
 * @file gi_probe_sample.c
 * @brief Nearest-probe SH sampler (see gi_probe_sample.h).
 */
#include "ferrum/renderer/gi/gi_probe_sample.h"

#include <stddef.h>

#include "ferrum/lightmap/lm_sh.h"

#define GI_SAMPLE_MAX 32u   /* bounded candidate gather (cell + 26 neighbours). */

bool gi_probe_sample(const gi_probe_set_t *set, const gi_probe_grid_t *grid,
                     const float p[3], const float n[3], float out_rgb[3])
{
    if (out_rgb != NULL) out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
    if (set == NULL || grid == NULL || p == NULL || n == NULL || out_rgb == NULL)
        return false;

    uint32_t cand[GI_SAMPLE_MAX];
    uint32_t m = gi_probe_grid_gather(grid, p[0], p[1], p[2], cand, GI_SAMPLE_MAX);
    if (m == 0u)
        return false;

    /* Inverse-distance weighted blend of the candidates' SH (per channel). */
    lm_sh9_t blend[3];
    for (int c = 0; c < 3; ++c) lm_sh9_zero(&blend[c]);
    float wsum = 0.0f;
    for (uint32_t i = 0; i < m; ++i) {
        uint32_t pi = cand[i];
        float dx = p[0]-set->pos[pi*3], dy = p[1]-set->pos[pi*3+1],
              dz = p[2]-set->pos[pi*3+2];
        float w = 1.0f / (dx*dx + dy*dy + dz*dz + 1e-4f);
        wsum += w;
        for (int c = 0; c < 3; ++c)
            for (int b = 0; b < 9; ++b)
                blend[c].c[b] += set->sh[pi*27 + c*9 + b] * w;
    }
    if (wsum <= 0.0f)
        return false;
    float inv = 1.0f / wsum;
    vec3_t nn = { n[0], n[1], n[2] };
    for (int c = 0; c < 3; ++c) {
        for (int b = 0; b < 9; ++b) blend[c].c[b] *= inv;
        float e = lm_sh9_irradiance(&blend[c], nn);
        out_rgb[c] = e < 0.0f ? 0.0f : e;   /* SH can ring slightly negative. */
    }
    return true;
}
