/**
 * @file probe_bake_place_run.c
 * @brief Offline post-bake placement composition (see probe_bake_place.h).
 */
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/probe_file.h"
#include "ferrum/probe/probe_set.h"
#include "ferrum/probe/place/probe_bake_place.h"
#include "ferrum/probe/place/probe_brick_file.h"

bool probe_bake_place_run(const probe_brick_config_t *brick,
                          const probe_fixup_config_t *fixup,
                          struct arena *arena, const char *out_path,
                          const char *bricks_path, uint32_t *out_count)
{
    if (brick == NULL || arena == NULL || out_path == NULL) return false;

    /* 1. Ternary brick placement over the baked field. */
    probe_set_t set;
    probe_brick_t *bricks = NULL;
    uint32_t n_bricks = 0;
    if (!probe_brick_place(brick, arena, &set, &bricks, &n_bricks)) return false;

    /* 2. Optional fix-up: adjusted trace origins + validity. In points-only
     * mode invalid probes are compacted away; in bricks mode ALL probes stay
     * (the bricks' probe_idx tables reference them by index) and validity is
     * shipped for the sampler to mask by. */
    arena_t *ar = (arena_t *)arena;
    uint8_t *valid = NULL;
    if (fixup != NULL && set.count > 0) {
        float *adjusted = arena_alloc(ar, 16u, (size_t)set.count * 3u * sizeof(float));
        valid = arena_alloc(ar, 16u, set.count);
        if (adjusted == NULL || valid == NULL) return false;
        if (!probe_fixup_apply(fixup, set.positions, set.count, adjusted, valid))
            return false;
        set.positions = adjusted;
        if (bricks_path == NULL) {
            uint32_t kept = 0;
            for (uint32_t i = 0; i < set.count; ++i) {
                if (!valid[i]) continue;
                if (kept != i)
                    memcpy(&adjusted[(size_t)kept * 3u], &adjusted[(size_t)i * 3u],
                           3u * sizeof(float));
                ++kept;
            }
            set.count = kept;
        }
    }

    /* 2b. Bricks mode: ship the sampling structure + validity. */
    if (bricks_path != NULL) {
        if (valid == NULL && set.count > 0) {
            /* No fixup ran: every probe is valid. */
            valid = arena_alloc(ar, 16u, set.count);
            if (valid == NULL) return false;
            memset(valid, 1, set.count);
        }
        probe_brick_data_t bd;
        memset(&bd, 0, sizeof bd);
        /* save requires a non-NULL brick pointer even when empty. */
        static probe_brick_t no_bricks;
        bd.bricks = (n_bricks > 0) ? bricks : &no_bricks;
        bd.n_bricks = n_bricks;
        bd.valid = valid;
        bd.n_probes = set.count;
        bd.coarse_brick = brick->coarse_brick;
        bd.levels = brick->levels;
        memcpy(bd.aabb_min, brick->aabb_min, sizeof bd.aabb_min);
        memcpy(bd.aabb_max, brick->aabb_max, sizeof bd.aabb_max);
        if (!probe_brick_data_save(bricks_path, &bd)) return false;
    }

    /* 3. Ship as manual probes: an unstructured point set. probe_set_save
     * requires a non-NULL position pointer even for an empty set. */
    static const float no_positions[3] = { 0.0f, 0.0f, 0.0f };
    if (set.count == 0) set.positions = (float *)no_positions;
    memset(set.grid_dim, 0, sizeof set.grid_dim);
    set.sh_coeffs = 0;
    set.sh = NULL;
    if (!probe_set_save(out_path, &set)) return false;
    if (out_count != NULL) *out_count = set.count;
    return true;
}
