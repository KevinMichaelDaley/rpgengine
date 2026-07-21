/**
 * @file probe_fixup_apply.c
 * @brief Probe virtual-offset + validity fix-up (see probe_fixup.h).
 *
 * Iterative gradient walk rather than a single closed-form push: near thin or
 * concave geometry the finite-difference gradient changes along the path, so
 * short re-evaluated steps escape cases a one-shot displacement overshoots.
 * Step budget and displacement cap bound the walk; everything is pure float
 * math on the caller's arrays (no allocation, fiber-safe).
 */
#include <math.h>
#include <string.h>

#include "ferrum/probe/place/probe_fixup.h"

/* Central-difference SDF gradient; falls back to +Y when degenerate (flat
 * regions of a bad SDF), so a buried probe still moves somewhere sensible. */
static void fixup_gradient(const probe_fixup_config_t *cfg, const float p[3],
                           float out[3])
{
    const float h = 0.02f;
    for (int a = 0; a < 3; ++a) {
        float pp[3], pm[3];
        memcpy(pp, p, sizeof pp);
        memcpy(pm, p, sizeof pm);
        pp[a] += h; pm[a] -= h;
        out[a] = cfg->sdf(pp, cfg->sdf_user) - cfg->sdf(pm, cfg->sdf_user);
    }
    float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len < 1e-6f) { out[0] = 0.0f; out[1] = 1.0f; out[2] = 0.0f; return; }
    for (int a = 0; a < 3; ++a) out[a] /= len;
}

/* Walk one probe to clearance. Returns 1 if it reached clearance (valid). */
static int fixup_walk(const probe_fixup_config_t *cfg, const float p_in[3],
                      float p_out[3])
{
    memcpy(p_out, p_in, 3 * sizeof(float));
    float sd = cfg->sdf(p_out, cfg->sdf_user);
    if (sd >= cfg->clearance) return 1;   /* already clear: bit-exact copy. */

    float moved = 0.0f;
    /* Enough steps to cross max_push in deficit-sized increments. */
    for (int it = 0; it < 32; ++it) {
        float g[3];
        fixup_gradient(cfg, p_out, g);
        /* Step by the clearance deficit (+bias on the final approach), capped
         * by the remaining displacement budget. */
        float want = (cfg->clearance - sd) + cfg->bias;
        float budget = cfg->max_push - moved;
        if (budget <= 0.0f) break;
        float step = want < budget ? want : budget;
        for (int a = 0; a < 3; ++a) p_out[a] += g[a] * step;
        moved += step;
        sd = cfg->sdf(p_out, cfg->sdf_user);
        if (sd >= cfg->clearance) return 1;
    }
    return sd >= cfg->clearance;
}

bool probe_fixup_apply(const probe_fixup_config_t *cfg, const float *positions,
                       uint32_t count, float *adjusted, uint8_t *valid)
{
    if (count == 0) return true;   /* nothing to do is success. */
    if (cfg == NULL || cfg->sdf == NULL || positions == NULL ||
        adjusted == NULL || valid == NULL)
        return false;

    /* clearance <= 0: the pass is disabled -- verbatim copy, all valid. */
    if (cfg->clearance <= 0.0f) {
        memcpy(adjusted, positions, (size_t)count * 3u * sizeof(float));
        memset(valid, 1, count);
        return true;
    }

    for (uint32_t i = 0; i < count; ++i)
        valid[i] = (uint8_t)fixup_walk(cfg, &positions[(size_t)i * 3u],
                                       &adjusted[(size_t)i * 3u]);
    return true;
}
