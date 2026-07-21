/**
 * @file gi_zone_sdf.h
 * @brief Global low-res ZONE SDF composed from the bake's fine per-chunk SDFs.
 *
 * The probe GI pages fine SDF chunks into a bounded GPU pool; once a zone has
 * more chunks than slots, rays inside a NON-RESIDENT chunk's box used to see
 * empty space and sail through walls (light leak). The zone SDF is the page-fault
 * fallback: one coarse distance+albedo field covering the whole zone, always
 * resident, sampled wherever fine coverage is not. Composed FROM the bake output
 * (the written _cNNN.sdf chunks), one per zone; a level is the one-zone case.
 *
 * The compose is CONSERVATIVE: each coarse cell takes the MINIMUM fine distance
 * inside it, so thin walls survive any resolution drop -- a sphere march against
 * the coarse field stops at (or before) every surface the fine field had, never
 * after. Albedo follows the minimum-distance sample (the nearest surface).
 *
 * Headless (no GL), unit-testable; the baker writes the result via lm_sdf_save_rgba
 * ("<prefix>_zone.sdf") and the runtime uploads it (gi_sdf_stream).
 *
 * Ownership: pure functions; all buffers are caller-owned. Never allocates.
 */
#ifndef FERRUM_RENDERER_GI_GI_ZONE_SDF_H
#define FERRUM_RENDERER_GI_GI_ZONE_SDF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** One fine source chunk (a loaded _cNNN.sdf): borrowed field data + placement. */
typedef struct gi_zone_sdf_src {
    const float *dist;    /**< [dims^3] distances (required). */
    const float *albedo;  /**< [dims^3 * 3] RGB, or NULL (v1 chunk -> mid-grey). */
    int32_t      dims[3];
    float        voxel;
    float        origin[3];
} gi_zone_sdf_src_t;

/**
 * @brief Plan the zone grid over the UNION of the source chunk boxes: a uniform
 *        voxel sized so the longest union extent spans @p max_dim cells.
 * @return false on NULL/empty sources or max_dim < 1. Fills @p out_dims (each
 *         >= 1, <= max_dim), @p out_voxel and @p out_origin (union min corner).
 */
bool gi_zone_sdf_plan(const gi_zone_sdf_src_t *srcs, uint32_t n_srcs,
                      int32_t max_dim, int32_t out_dims[3], float *out_voxel,
                      float out_origin[3]);

/**
 * @brief Compose the coarse field: min-downsample every source voxel into the
 *        planned grid. Cells no source covers keep a LARGE positive distance
 *        (empty space); covered cells hold the minimum fine distance and the
 *        albedo of that minimum sample.
 * @param out_dist   [dims product] caller-owned, fully written.
 * @param out_albedo [dims product * 3] caller-owned, fully written.
 * @param cap        capacity of @p out_dist in cells (albedo must hold cap*3).
 * @return false on NULL args or if the planned grid exceeds @p cap.
 */
bool gi_zone_sdf_compose(const gi_zone_sdf_src_t *srcs, uint32_t n_srcs,
                         const int32_t dims[3], float voxel, const float origin[3],
                         float *out_dist, float *out_albedo, uint32_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_GI_ZONE_SDF_H */
