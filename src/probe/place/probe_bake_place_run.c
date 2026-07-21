/**
 * @file probe_bake_place_run.c
 * @brief Offline post-bake placement composition (see probe_bake_place.h).
 */
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/probe_file.h"
#include "ferrum/probe/probe_set.h"
#include "ferrum/probe/place/probe_bake_place.h"

bool probe_bake_place_run(const probe_brick_config_t *brick,
                          const probe_fixup_config_t *fixup,
                          struct arena *arena, const char *out_path,
                          uint32_t *out_count)
{
    if (brick == NULL || arena == NULL || out_path == NULL) return false;

    /* 1. Ternary brick placement over the baked field. */
    probe_set_t set;
    probe_brick_t *bricks = NULL;
    uint32_t n_bricks = 0;
    if (!probe_brick_place(brick, arena, &set, &bricks, &n_bricks)) return false;

    /* 2. Optional fix-up: adjusted trace origins + validity, invalid dropped.
     * The compaction happens in place over the adjusted array -- the lattice
     * identity is irrelevant in the shipped file (manual probes are points). */
    if (fixup != NULL && set.count > 0) {
        arena_t *ar = (arena_t *)arena;
        float *adjusted = arena_alloc(ar, 16u, (size_t)set.count * 3u * sizeof(float));
        uint8_t *valid = arena_alloc(ar, 16u, set.count);
        if (adjusted == NULL || valid == NULL) return false;
        if (!probe_fixup_apply(fixup, set.positions, set.count, adjusted, valid))
            return false;
        uint32_t kept = 0;
        for (uint32_t i = 0; i < set.count; ++i) {
            if (!valid[i]) continue;
            if (kept != i)
                memcpy(&adjusted[(size_t)kept * 3u], &adjusted[(size_t)i * 3u],
                       3u * sizeof(float));
            ++kept;
        }
        set.positions = adjusted;
        set.count = kept;
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
