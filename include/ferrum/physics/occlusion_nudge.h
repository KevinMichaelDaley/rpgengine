/**
 * @file occlusion_nudge.h
 * @brief Position nudge utility for bodies re-promoted after occlusion.
 *
 * When a body transitions from occluded (T3) back to a near tier (T0–T1),
 * its simulated position may have drifted from its visual target.  This
 * utility lerps the body toward the target position over several frames,
 * capping total correction at 5 mm to prevent visual pops.
 */
#ifndef FERRUM_PHYSICS_OCCLUSION_NUDGE_H
#define FERRUM_PHYSICS_OCCLUSION_NUDGE_H

#include <stdint.h>

struct phys_body;
struct vec3;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Apply a position nudge for re-promoted bodies.
 *
 * For each body where repromotion_flags[i] is non-zero, lerps
 * body.position toward target_positions[i] by 1/nudge_frames of the
 * delta, capped at 5 mm / nudge_frames per call.
 *
 * @param bodies             Body array (read/write).  Must not be NULL
 *                           when body_count > 0.
 * @param repromotion_flags  Per-body flag (1 = needs nudge).  Must not
 *                           be NULL when body_count > 0.
 * @param target_positions   Per-body target position.  Must not be NULL
 *                           when body_count > 0.
 * @param body_count         Number of bodies.
 * @param nudge_frames       Number of frames over which to spread the
 *                           correction (typically 3–5).  Clamped to
 *                           minimum 1.
 *
 * Ownership: borrows all pointers; does not free anything.
 * Side effects: writes to bodies[i].position for flagged bodies.
 */
void phys_occlusion_nudge_apply(struct phys_body *bodies,
                                const uint8_t *repromotion_flags,
                                const struct vec3 *target_positions,
                                uint32_t body_count,
                                uint32_t nudge_frames);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_OCCLUSION_NUDGE_H */
