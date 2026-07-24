/**
 * @file gi_runtime_probe_gain.c
 * @brief Runtime gain on the probe DIFFUSE ambient (see gi_runtime.h).
 *
 * Scenes that stream BAKED probe SH freeze the updater, so none of the
 * probe-update tuning (gi_dyn_gain, gi_stat_scale, ...) can rescale their
 * ambient at runtime -- this composite-side gain is the lever ("turn up the
 * sun GI" on a frozen bake). Uploaded as u_gi_probe_gain and applied to the
 * gi_dyn term of the receiver's irradiance composite.
 */
#include "ferrum/renderer/gi/gi_runtime.h"

void gi_runtime_set_probe_gain(gi_runtime_t *gi, float gain)
{
    if (gi == NULL)
        return;
    gi->probe_gain = gain > 0.0f ? gain : 0.0f;
}
