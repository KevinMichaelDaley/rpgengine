/**
 * @file broadphase.c
 * @brief Broadphase collision detection stage implementation.
 *
 * Iterates active tier bodies (T0–T4), queries the spatial grid for
 * AABB overlap candidates, performs precise overlap tests, excludes
 * static-static pairs, and outputs canonical (body_a < body_b) pairs.
 */

#include "ferrum/physics/broadphase.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/static_bvh.h"
#include "ferrum/physics/tier_list.h"

/** Maximum candidates returned by a single grid query. */
#define BROADPHASE_MAX_CANDIDATES 256

#define BROADPHASE_STATIC_QUERY_STACK_CAP 128u

static uint32_t broadphase_grid_hash(int32_t x, int32_t y, int32_t z) {
    return ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^
           ((uint32_t)z * 83492791u);
}

static bool broadphase_aabb_touches_static_bucket_flags(
    const phys_spatial_grid_t *grid,
    const phys_aabb_t *aabb,
    const uint8_t *bucket_flags,
    uint32_t bucket_flag_count) {

    if (!grid || !aabb || !bucket_flags || bucket_flag_count == 0) {
        return true;
    }
    if (!grid->cells || grid->cell_count != bucket_flag_count) {
        return true;
    }

    if (isnan(aabb->min.x) || isnan(aabb->min.y) || isnan(aabb->min.z) ||
        isnan(aabb->max.x) || isnan(aabb->max.y) || isnan(aabb->max.z) ||
        isinf(aabb->min.x) || isinf(aabb->min.y) || isinf(aabb->min.z) ||
        isinf(aabb->max.x) || isinf(aabb->max.y) || isinf(aabb->max.z)) {
        return false;
    }

    int32_t min_cx = (int32_t)floorf(aabb->min.x * grid->inv_cell_size);
    int32_t min_cy = (int32_t)floorf(aabb->min.y * grid->inv_cell_size);
    int32_t min_cz = (int32_t)floorf(aabb->min.z * grid->inv_cell_size);
    int32_t max_cx = (int32_t)floorf(aabb->max.x * grid->inv_cell_size);
    int32_t max_cy = (int32_t)floorf(aabb->max.y * grid->inv_cell_size);
    int32_t max_cz = (int32_t)floorf(aabb->max.z * grid->inv_cell_size);

    int32_t max_cells_per_axis = (int32_t)grid->cell_count;
    if ((max_cx - min_cx) > max_cells_per_axis) { max_cx = min_cx + max_cells_per_axis; }
    if ((max_cy - min_cy) > max_cells_per_axis) { max_cy = min_cy + max_cells_per_axis; }
    if ((max_cz - min_cz) > max_cells_per_axis) { max_cz = min_cz + max_cells_per_axis; }

    for (int32_t cz = min_cz; cz <= max_cz; ++cz) {
        for (int32_t cy = min_cy; cy <= max_cy; ++cy) {
            for (int32_t cx = min_cx; cx <= max_cx; ++cx) {
                uint32_t idx = broadphase_grid_hash(cx, cy, cz) & grid->cell_mask;
                if (bucket_flags[idx]) {
                    return true;
                }
            }
        }
    }

    return false;
}

static void broadphase_emit_static_pairs_for_body(
    const phys_broadphase_args_t *args,
    uint32_t body_a,
    uint32_t *io_pair_count) {

    if (!args || !args->static_bvh || !args->static_bvh->nodes ||
        args->static_bvh->node_count == 0 || !io_pair_count) {
        return;
    }

    const phys_aabb_t *aabb_a = &args->aabbs[body_a];

    if (args->static_bucket_flags &&
        args->static_bucket_flag_count == args->grid->cell_count) {
        if (!broadphase_aabb_touches_static_bucket_flags(
                args->grid, aabb_a, args->static_bucket_flags,
                args->static_bucket_flag_count)) {
            return;
        }
    }

    uint32_t stack[BROADPHASE_STATIC_QUERY_STACK_CAP];
    uint32_t sp = 0;

    if (args->static_bvh->root >= args->static_bvh->node_count) {
        return;
    }

    stack[sp++] = args->static_bvh->root;

    while (sp) {
        uint32_t node_idx = stack[--sp];
        if (node_idx >= args->static_bvh->node_count) {
            continue;
        }

        const phys_static_bvh_node_t *n = &args->static_bvh->nodes[node_idx];
        if (!phys_aabb_overlap(&n->bounds, aabb_a)) {
            continue;
        }

        if (phys_static_bvh_node_is_leaf(n)) {
            uint32_t body_b = n->item_id;
            if (body_b == body_a) {
                continue;
            }

            uint32_t lo = (body_a < body_b) ? body_a : body_b;
            uint32_t hi = (body_a < body_b) ? body_b : body_a;

            if (!phys_aabb_overlap(&args->aabbs[lo], &args->aabbs[hi])) {
                continue;
            }

            if (*io_pair_count < args->max_pairs) {
                args->pairs_out[*io_pair_count].body_a = lo;
                args->pairs_out[*io_pair_count].body_b = hi;
                (*io_pair_count)++;
            }
        } else {
            if (sp + 2u > BROADPHASE_STATIC_QUERY_STACK_CAP) {
                break;
            }
            stack[sp++] = n->left;
            stack[sp++] = n->right;
        }
    }
}

void phys_stage_broadphase(const phys_broadphase_args_t *args) {
    if (!args) {
        return;
    }
    if (!args->bodies || !args->aabbs || !args->grid ||
        !args->tier_lists || !args->pairs_out || !args->pair_count_out) {
        if (args->pair_count_out) {
            *args->pair_count_out = 0;
        }
        return;
    }

    uint32_t pair_count = 0;

    /* Iterate active tiers T0 through T4. */
    for (int tier = PHYS_TIER_0_DIRECT; tier <= PHYS_TIER_4_BACKGROUND; ++tier) {
        const phys_tier_list_t *list = &args->tier_lists->tiers[tier];

        for (uint32_t i = 0; i < list->count; ++i) {
            uint32_t body_a = list->indices[i];
            const phys_aabb_t *aabb_a = &args->aabbs[body_a];

            /* Query spatial grid for candidate overlaps. */
            uint32_t candidates[BROADPHASE_MAX_CANDIDATES];
            uint32_t cand_count = phys_spatial_grid_query(
                args->grid, aabb_a, candidates, BROADPHASE_MAX_CANDIDATES);

            const bool use_static_bvh =
                (args->static_bvh && args->static_bvh->nodes &&
                 args->static_bvh->node_count > 0);

            for (uint32_t j = 0; j < cand_count; ++j) {
                uint32_t body_b = candidates[j];

                /* Skip self-pairs. */
                if (body_a == body_b) {
                    continue;
                }

                /* When a static BVH is provided, static pairs are emitted via
                 * BVH query (not grid candidates). */
                if (use_static_bvh && phys_body_is_static(&args->bodies[body_b])) {
                    continue;
                }

                /* Canonical order: lo < hi.
                 *
                 * - dynamic-dynamic: skip when body_a > body_b to avoid duplicates.
                 * - dynamic-static: if not using static BVH, emit even when
                 *   body_a > body_b (static bodies are not in tier lists).
                 */
                uint32_t lo, hi;
                if (body_a < body_b) {
                    lo = body_a;
                    hi = body_b;
                } else {
                    if (use_static_bvh) {
                        continue;
                    }
                    if (!phys_body_is_static(&args->bodies[body_b])) {
                        continue;
                    }
                    lo = body_b;
                    hi = body_a;
                }

                /* Skip static-static pairs (both inv_mass == 0, non-kinematic). */
                if (phys_body_is_static(&args->bodies[lo]) &&
                    phys_body_is_static(&args->bodies[hi])) {
                    continue;
                }

                /* Precise AABB overlap test. */
                if (!phys_aabb_overlap(&args->aabbs[lo], &args->aabbs[hi])) {
                    continue;
                }

                /* Emit pair if buffer has room. */
                if (pair_count < args->max_pairs) {
                    args->pairs_out[pair_count].body_a = lo;
                    args->pairs_out[pair_count].body_b = hi;
                    pair_count++;
                }
            }

            if (use_static_bvh) {
                broadphase_emit_static_pairs_for_body(args, body_a, &pair_count);
            }
        }
    }

    /* ── Halfspace pass ──────────────────────────────────────────── */
    /* Halfspaces are infinite planes — they cannot participate in a
     * spatial grid or BVH.  Pair every active tiered body with each
     * halfspace body directly. */
    for (uint32_t h = 0; h < args->halfspace_body_count; ++h) {
        uint32_t hs_body = args->halfspace_bodies[h];

        for (int tier = PHYS_TIER_0_DIRECT; tier <= PHYS_TIER_4_BACKGROUND; ++tier) {
            const phys_tier_list_t *list = &args->tier_lists->tiers[tier];
            for (uint32_t i = 0; i < list->count; ++i) {
                uint32_t dyn = list->indices[i];
                if (dyn == hs_body) continue;

                uint32_t lo = (dyn < hs_body) ? dyn : hs_body;
                uint32_t hi = (dyn < hs_body) ? hs_body : dyn;

                if (pair_count < args->max_pairs) {
                    args->pairs_out[pair_count].body_a = lo;
                    args->pairs_out[pair_count].body_b = hi;
                    pair_count++;
                }
            }
        }
    }

    *args->pair_count_out = pair_count;
}
