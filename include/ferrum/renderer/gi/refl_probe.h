/**
 * @file refl_probe.h
 * @brief Sparse cubemap reflection probes (rpg-akwc): the probe record and
 *        the probe-set container shared by placement, the bake, the .rprobe
 *        file and the runtime binder.
 *
 * Reflection probes are a SEPARATE, much sparser set than the irradiance
 * probes: each one owns an octahedral tile in a prefiltered radiance atlas
 * (RGB = radiance with an irradiance term, A = specular occlusion cone-traced
 * from the baked SDF) plus a scalar mean occlusion @c ao used to modulate the
 * SG-probe specular that fills the gaps between cubemaps.
 *
 * Ownership: the set never allocates -- @c probes is caller-owned storage
 * passed to @ref refl_probe_set_init. All APIs are NULL-tolerant and return
 * error through their result values (no aborts).
 */
#ifndef FERRUM_RENDERER_GI_REFL_PROBE_H
#define FERRUM_RENDERER_GI_REFL_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hard cap on the filtered mip chain stored in the atlas / .rprobe file. */
#define REFL_PROBE_MAX_MIPS 6u

/** One reflection probe: world position, mean baked occlusion, atlas tile. */
typedef struct refl_probe {
    float pos[3];   /**< world-space probe centre. */
    float ao;       /**< mean specular occlusion over the sphere, 0..1. */
    uint32_t tile;  /**< octahedral tile index into the atlas (row-major). */
} refl_probe_t;

/** A set of reflection probes + the atlas layout they share. */
typedef struct refl_probe_set {
    refl_probe_t *probes;  /**< caller-owned storage, @c capacity entries. */
    uint32_t count;        /**< probes in use. */
    uint32_t capacity;     /**< storage size of @c probes. */
    uint32_t tile_res;     /**< octahedral tile edge in texels at mip 0. */
    uint32_t mips;         /**< filtered mip levels (1..REFL_PROBE_MAX_MIPS). */
    uint32_t tiles_x;      /**< atlas tile-grid width. */
    uint32_t tiles_y;      /**< atlas tile-grid height. */
} refl_probe_set_t;

/**
 * Initialise @p set over caller-owned @p storage (may be NULL when
 * @p capacity is 0). Count and the atlas layout fields reset to zero.
 * NULL @p set is ignored. No allocation, no side effects beyond @p set.
 */
void refl_probe_set_init(refl_probe_set_t *set, refl_probe_t *storage,
                         uint32_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_PROBE_H */
