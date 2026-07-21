/**
 * @file probe_brick_file.h
 * @brief .bricks sidecar IO for brick-placed probes (rpg-pjkb, feature 5).
 *
 * The forward pass samples probes through the brick structure (fragment ->
 * voxel -> brick -> 8 of its 64 probes), so the offline pass ships the bricks
 * themselves -- with per-probe validity -- alongside the .probes positions.
 * The dense voxel index is NOT stored: the loader rebuilds it in milliseconds
 * with probe_brick_index_build from the grid parameters carried here.
 *
 * Format (native-endian): magic "PBK1", u32 n_bricks, u32 n_probes,
 * f32 coarse_brick, i32 levels, f32 aabb_min[3], f32 aabb_max[3],
 * then n_bricks raw probe_brick_t, then n_probes validity bytes.
 *
 * Ownership: load carves bricks+valid from the caller arena. Errors: bool,
 * no partial output. Side effects: save writes the file.
 */
#ifndef FERRUM_PROBE_PLACE_PROBE_BRICK_FILE_H
#define FERRUM_PROBE_PLACE_PROBE_BRICK_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/probe/place/probe_brick.h"

struct arena; /* ferrum/memory/arena.h */

/** A shipped brick set: bricks + per-probe validity + the grid parameters the
 *  voxel index is rebuilt from (pass them to probe_brick_index_build). */
typedef struct probe_brick_data {
    probe_brick_t *bricks;      /**< [n_bricks]. */
    uint32_t       n_bricks;
    uint8_t       *valid;       /**< [n_probes] 1 = usable, 0 = masked. */
    uint32_t       n_probes;
    float          coarse_brick;/**< placement grid parameters... */
    int32_t        levels;
    float          aabb_min[3];
    float          aabb_max[3];
} probe_brick_data_t;

/**
 * @brief Write @p d to @p path. False on NULL path/d, NULL d->bricks (or NULL
 *        d->valid with n_probes > 0), or IO error.
 */
bool probe_brick_data_save(const char *path, const probe_brick_data_t *d);

/**
 * @brief Load @p path into @p out (bricks+valid from @p arena). False on NULL
 *        args, IO error, bad magic, corrupt counts, or arena exhaustion.
 */
bool probe_brick_data_load(const char *path, struct arena *arena,
                           probe_brick_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROBE_PLACE_PROBE_BRICK_FILE_H */
