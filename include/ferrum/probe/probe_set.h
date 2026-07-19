/**
 * @file probe_set.h
 * @brief A placed set of GI probes (rpg-ft0g): world positions plus an optional
 *        regular-grid layout and optional baked SH. Headless (no GL).
 *
 * When @c grid_dim[0] > 0 the probes form a regular lattice (origin/cell/dim,
 * index order (z*dim[1]+y)*dim[0]+x) suitable for the trilinear GI path; when
 * @c grid_dim[0] == 0 the set is an unstructured point cloud (manual /
 * importance-refined / chunk-filtered), consumed via explicit probe positions.
 */
#ifndef FERRUM_PROBE_PROBE_SET_H
#define FERRUM_PROBE_PROBE_SET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief A set of probes with an optional grid layout and optional baked SH.
 *
 * Ownership: @c positions and @c sh point into a caller-supplied arena (from a
 * placement or load call) or caller-owned memory (when the caller builds a set
 * to save). @c positions has @c count*3 floats; @c sh has @c count*sh_coeffs
 * floats (NULL / 0 when no baked SH).
 */
typedef struct probe_set {
    uint32_t count;          /**< number of probes. */
    float   *positions;      /**< [count*3] world xyz. */
    float    grid_origin[3]; /**< regular-grid probe (0,0,0) (valid iff grid_dim[0]>0). */
    float    grid_cell[3];   /**< per-axis spacing between adjacent grid probes. */
    int32_t  grid_dim[3];    /**< pnx,pny,pnz; 0,0,0 => not a regular grid. */
    uint32_t sh_coeffs;      /**< floats of baked SH per probe (0 = none). */
    float   *sh;             /**< [count*sh_coeffs] baked SH, or NULL. */
} probe_set_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROBE_PROBE_SET_H */
