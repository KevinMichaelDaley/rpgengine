/**
 * @file gi_runtime_static.c
 * @brief Bind a static irradiance volume to the probe update (see gi_runtime.h).
 *
 * Split from gi_runtime.c to keep each source file within the 4-function rule.
 */
#include "ferrum/renderer/gi/gi_runtime.h"
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
    for (int i = 0; i < 3; ++i) {
        gi->probe_grid_origin[i] = on ? origin[i] : 0.0f;
        gi->probe_grid_cell[i] = on ? cell[i] : 1.0f;
        gi->probe_grid_dim[i] = on ? dim[i] : 0;
    }
}
