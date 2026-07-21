/**
 * @file probe_bake_place.h
 * @brief Offline post-bake probe placement pass (rpg-pjkb, feature 4).
 *
 * Composition of the placement pipeline for the OFFLINE tool: ternary brick
 * placement (probe_brick) -> virtual-offset/validity fix-up (probe_fixup) ->
 * save the surviving probes as a manual .probes file (probe_file). The loader
 * then simply loads the file -- it never re-places, because at load time it
 * only ever sees the resident subset of the scene.
 *
 * Two output modes. POINTS-ONLY (@p bricks_path NULL): invalid probes are
 * DROPPED from the .probes file (as loose manual probes they would only
 * contribute garbage). BRICKS (@p bricks_path given): the runtime samples
 * through the brick structure (voxel -> brick -> 8 probes), whose probe_idx
 * tables must stay stable -- so ALL probes are kept (adjusted origins) and
 * the per-probe VALIDITY ships in the .bricks sidecar for the sampler to
 * mask by. Hand-placed extras append to the .probes file either way.
 *
 * Ownership: intermediates are carved from the caller arena; the file is the
 * output. Errors: false on NULL brick/arena/path, invalid brick config, arena
 * exhaustion, or file IO failure. Side effect: writes @p out_path.
 */
#ifndef FERRUM_PROBE_PLACE_PROBE_BAKE_PLACE_H
#define FERRUM_PROBE_PLACE_PROBE_BAKE_PLACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/probe/place/probe_brick.h"
#include "ferrum/probe/place/probe_fixup.h"

struct arena; /* ferrum/memory/arena.h */

/**
 * @brief Place, fix up, and save probes for a baked scene.
 *
 * @param brick     placement parameters (SDF callback included).
 * @param fixup     fix-up parameters, or NULL to save raw lattice positions.
 *                  When given, saved positions are the ADJUSTED trace origins
 *                  and invalid probes are dropped.
 * @param arena     backing for all intermediates.
 * @param out_path    .probes file to write.
 * @param bricks_path .bricks sidecar to write, or NULL for points-only mode.
 * @param out_count   receives the saved probe count (nullable).
 * @return true on success (zero probes still writes a valid empty file).
 */
bool probe_bake_place_run(const probe_brick_config_t *brick,
                          const probe_fixup_config_t *fixup,
                          struct arena *arena, const char *out_path,
                          const char *bricks_path, uint32_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROBE_PLACE_PROBE_BAKE_PLACE_H */
