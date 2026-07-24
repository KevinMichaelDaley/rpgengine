/**
 * @file refl_file.h
 * @brief .rprobe sidecar (rpg-akwc): the baked reflection-probe set + its
 *        prefiltered octahedral atlas mip chain.
 *
 * Layout (native endian):
 *   char  magic[4] = "RFP2"
 *   u32   count, tile_res, mips, tiles_x, tiles_y, depth_res
 *   per probe: float pos[3], float ao, u32 tile
 *   per mip m in [0, mips): float rgba[w_m * h_m * 4]   (atlas dims at m)
 *   if depth_res > 0: float rg[tiles_x*depth_res * tiles_y*depth_res * 2]
 *     -- the octahedral visibility-depth atlas (radial mean, mean^2), the
 *     DDGI-style Chebyshev test that keeps fragments from sampling probes
 *     they cannot see.
 *
 * Load allocates each mip payload with malloc (one-shot load-time
 * allocation; caller frees each out_mips[m]). Probe records land in the
 * caller's set storage (capacity-checked).
 */
#ifndef FERRUM_RENDERER_GI_REFL_FILE_H
#define FERRUM_RENDERER_GI_REFL_FILE_H

#include <stdbool.h>

#include "ferrum/renderer/gi/refl_probe.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Save @p set + the atlas mip payloads @p mips (mips[m] = full RGBA32F
 * atlas at level m, set->mips entries) + the RG32F visibility-depth atlas
 * @p depth (required when set->depth_res > 0, ignored otherwise). Returns
 * false on NULL args, degenerate layout, or I/O failure (partial file may
 * remain).
 */
bool refl_file_save(const char *path, const refl_probe_set_t *set,
                    const float *const mips[], const float *depth);

/**
 * Load @p path into @p set (records copied into its caller-owned storage),
 * malloc'd mip payloads into @p out_mips[0..mips) and the malloc'd depth
 * atlas into @p *out_depth (NULL when depth_res == 0). On ANY failure
 * (bad magic, truncation, capacity overflow) returns false with no
 * buffers handed back. Ownership of out_mips[m] transfers on success.
 */
bool refl_file_load(const char *path, refl_probe_set_t *set,
                    float *out_mips[REFL_PROBE_MAX_MIPS],
                    float **out_depth);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_FILE_H */
