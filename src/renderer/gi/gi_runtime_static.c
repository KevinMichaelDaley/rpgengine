/**
 * @file gi_runtime_static.c
 * @brief Bind a static irradiance volume to the probe update (see gi_runtime.h).
 *
 * Split from gi_runtime.c to keep each source file within the 4-function rule.
 */
#include "ferrum/renderer/gi/gi_runtime.h"

#include <stdio.h>
#include "ferrum/renderer/gi/gi_probe_gpu.h"

void gi_runtime_set_static_volume(gi_runtime_t *gi, unsigned int tex,
                                  const float origin[3], const float dim[3],
                                  float vox, float k)
{
    if (gi == NULL) return;
    gi_probe_gpu_set_static(&gi->gpu, tex, origin, dim, vox, k);
}

void gi_runtime_set_static_weights(gi_runtime_t *gi, float baked_w, float dyn_w)
{
    if (gi == NULL) return;
    gi->static_baked_w = baked_w;
    gi->static_dyn_w = dyn_w;
}

void gi_runtime_set_probe_grid(gi_runtime_t *gi, const float origin[3],
                               const float cell[3], const int dim[3])
{
    if (gi == NULL) return;
    int on = (origin && cell && dim && dim[0] > 0 && dim[1] > 0 && dim[2] > 0);
    gi->probe_grid_on = on;
    fprintf(stderr, "gi_runtime: probe grid %s (%dx%dx%d)\n",
            on ? "ON (trilinear)" : "OFF (froxel fallback)",
            dim ? dim[0] : 0, dim ? dim[1] : 0, dim ? dim[2] : 0);
    for (int i = 0; i < 3; ++i) {
        gi->probe_grid_origin[i] = on ? origin[i] : 0.0f;
        gi->probe_grid_cell[i] = on ? cell[i] : 1.0f;
        gi->probe_grid_dim[i] = on ? dim[i] : 0;
    }
}

void gi_runtime_set_sky_ao(gi_runtime_t *gi, const float color[3], float ref, float ao_mult)
{
    if (gi == NULL) return;
    for (int i = 0; i < 3; ++i) gi->sky_ao_color[i] = color ? color[i] : 0.0f;
    gi->sky_ao_ref = ref > 0.1f ? ref : 6.0f;
    gi->ao_mult = ao_mult < 0.0f ? 0.0f : (ao_mult > 1.0f ? 1.0f : ao_mult);
}

void gi_runtime_set_spec_gain(gi_runtime_t *gi, float gain)
{
    if (gi == NULL) return;
    gi->spec_gain = gain > 0.0f ? gain : 0.0f;
}

void gi_runtime_set_probe_active(gi_runtime_t *gi, const unsigned char *active,
                                 uint32_t n)
{
    if (gi == NULL) return;
    gi_probe_gpu_set_active(&gi->gpu, active, n);
}
