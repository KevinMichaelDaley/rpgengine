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
