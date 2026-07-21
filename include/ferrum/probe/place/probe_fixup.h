/**
 * @file probe_fixup.h
 * @brief Probe virtual-offset + validity fix-up (rpg-pjkb, feature 2).
 *
 * Bake/load-time counterpart of RTXGI relocation / Unity APV Virtual Offset
 * (ref/probe_placement_survey.md): a probe embedded in or pressed against
 * geometry gathers black and stamps dark seams onto nearby fragments, so its
 * TRACE ORIGIN is pushed out along the SDF gradient to a clearance. The push
 * is capped; a probe that cannot reach clearance within the cap is flagged
 * INVALID so sampling can zero its interpolation weight (the corner-validity
 * mask), instead of blending its garbage.
 *
 * Pure and headless: one SDF callback, caller-provided output arrays, no
 * allocation, deterministic. The LATTICE position remains the probe's identity
 * (grid addressing, froxel binning); only the adjusted trace origin moves.
 *
 * Ownership: caller owns all arrays. Nullability: sdf_user may be NULL; all
 * other pointers non-NULL when count > 0. Errors: returns false on NULL args
 * or NULL cfg->sdf, touching nothing. No side effects.
 */
#ifndef FERRUM_PROBE_PLACE_PROBE_FIXUP_H
#define FERRUM_PROBE_PLACE_PROBE_FIXUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Fix-up parameters.
 *
 * @c clearance is the target SDF value at the adjusted origin (<= 0 disables
 * the whole pass: positions copy through, all probes valid). @c bias is added
 * past clearance so the origin never sits exactly on the threshold. @c max_push
 * caps the total displacement; a probe still below clearance after a capped
 * push is reported invalid.
 */
typedef struct probe_fixup_config {
    float clearance;    /**< target min SDF at the trace origin (m). */
    float bias;         /**< extra push past clearance (m). */
    float max_push;     /**< displacement cap (m); exceeded => invalid. */
    float (*sdf)(const float p[3], void *user); /**< signed distance field. */
    void *sdf_user;     /**< opaque context for @c sdf (nullable). */
} probe_fixup_config_t;

/**
 * @brief Compute adjusted trace origins + validity for @p count probes.
 *
 * Walks each probe out along the (finite-difference) SDF gradient until
 * sdf >= clearance, then adds @c bias; total displacement is clamped to
 * @c max_push. Probes already at clearance are copied bit-exactly.
 *
 * @param positions  [count*3] lattice positions (unchanged).
 * @param adjusted   [count*3] receives the trace origins.
 * @param valid      [count] receives 1 = usable, 0 = could not escape.
 * @return true on success; false on NULL args (when count > 0) or NULL sdf.
 */
bool probe_fixup_apply(const probe_fixup_config_t *cfg, const float *positions,
                       uint32_t count, float *adjusted, uint8_t *valid);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROBE_PLACE_PROBE_FIXUP_H */
