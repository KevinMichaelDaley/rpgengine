/**
 * @file broadphase_par.c
 * @brief Parallel broadphase implementation.
 *
 * Collects active body indices into a flat array, splits into batches,
 * and dispatches each batch as a job.  Each job iterates its assigned
 * bodies, queries the spatial grid (read-only), and writes collision
 * pairs to the shared output buffer using an atomic pair counter.
 *
 * Non-static functions: 1 (phys_stage_broadphase_par).
 */

#include "ferrum/physics/par/broadphase_par.h"

#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/static_bvh.h"
#include "ferrum/physics/tier_list.h"

/** Maximum candidates returned by a single grid query. */
#define BROADPHASE_PAR_MAX_CANDIDATES 256

#define BROADPHASE_PAR_STATIC_QUERY_STACK_CAP 128u

static uint32_t broadphase_par_grid_hash(int32_t x, int32_t y, int32_t z) {
    return ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^
           ((uint32_t)z * 83492791u);
}

static bool broadphase_par_aabb_touches_static_bucket_flags(
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
                uint32_t idx = broadphase_par_grid_hash(cx, cy, cz) & grid->cell_mask;
                if (bucket_flags[idx]) {
                    return true;
                }
            }
        }
    }

    return false;
}

/* ── Shared context ─────────────────────────────────────────────── */

/**
 * @brief Shared context across all batches in a parallel broadphase.
 *
 * Each job receives a phys_job_batch_t whose user_args points here.
 * Jobs index into active_indices[start..start+count) and write pairs
 * to pairs_out using atomic_pair_count for contention-free indexing.
 */
typedef struct broadphase_par_shared {
    const phys_body_t        *bodies;       /**< Body array (read-only). */
    const phys_aabb_t        *aabbs;        /**< Per-body AABB array. */
    const phys_spatial_grid_t *grid;        /**< Spatial hash grid (read-only). */
    const uint32_t           *active_indices; /**< Flat array of active body indices. */

    const phys_static_bvh_t  *static_bvh;
    const uint8_t            *static_bucket_flags;
    uint32_t                  static_bucket_flag_count;

    phys_collision_pair_t    *pairs_out;     /**< Shared output buffer. */
    uint32_t                  max_pairs;     /**< Capacity of pairs_out. */
    atomic_uint               atomic_pair_count; /**< Atomic write index. */
} broadphase_par_shared_t;

/* ── Job function ───────────────────────────────────────────────── */

/**
 * @brief Job function: process a batch of active bodies.
 *
 * For each body in the batch, queries the spatial grid for candidate
 * overlaps, applies canonical ordering, static-static exclusion, and
 * precise AABB overlap tests, then writes pairs atomically.
 */
static void broadphase_par_batch_job(void *data) {
    phys_job_batch_t *batch = data;
    broadphase_par_shared_t *shared = batch->user_args;

    const bool use_static_bvh =
        (shared->static_bvh && shared->static_bvh->nodes &&
         shared->static_bvh->node_count > 0);

    uint32_t end = batch->start + batch->count;
    for (uint32_t idx = batch->start; idx < end; ++idx) {
        uint32_t body_a = shared->active_indices[idx];
        const phys_aabb_t *aabb_a = &shared->aabbs[body_a];

        /* Query spatial grid for candidate overlaps. */
        uint32_t candidates[BROADPHASE_PAR_MAX_CANDIDATES];
        uint32_t cand_count = phys_spatial_grid_query(
            shared->grid, aabb_a, candidates, BROADPHASE_PAR_MAX_CANDIDATES);

        for (uint32_t j = 0; j < cand_count; ++j) {
            uint32_t body_b = candidates[j];

            /* Skip self-pairs. */
            if (body_a == body_b) {
                continue;
            }

            /* When a static BVH is provided, static pairs are emitted via
             * BVH query (not grid candidates). */
            if (use_static_bvh && phys_body_is_static(&shared->bodies[body_b])) {
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
                if (!phys_body_is_static(&shared->bodies[body_b])) {
                    continue;
                }
                lo = body_b;
                hi = body_a;
            }

            /* Skip static-static pairs. */
            if (phys_body_is_static(&shared->bodies[lo]) &&
                phys_body_is_static(&shared->bodies[hi])) {
                continue;
            }

            /* Precise AABB overlap test. */
            if (!phys_aabb_overlap(&shared->aabbs[lo], &shared->aabbs[hi])) {
                continue;
            }

            /* Atomically claim a slot in the output buffer. */
            uint32_t slot = atomic_fetch_add_explicit(
                &shared->atomic_pair_count, 1, memory_order_relaxed);
            if (slot < shared->max_pairs) {
                shared->pairs_out[slot].body_a = lo;
                shared->pairs_out[slot].body_b = hi;
            }
        }

        if (use_static_bvh) {
            if (shared->static_bucket_flags &&
                shared->static_bucket_flag_count == shared->grid->cell_count) {
                if (!broadphase_par_aabb_touches_static_bucket_flags(
                        shared->grid, aabb_a, shared->static_bucket_flags,
                        shared->static_bucket_flag_count)) {
                    continue;
                }
            }

            uint32_t stack[BROADPHASE_PAR_STATIC_QUERY_STACK_CAP];
            uint32_t sp = 0;

            if (shared->static_bvh->root < shared->static_bvh->node_count) {
                stack[sp++] = shared->static_bvh->root;
            }

            while (sp) {
                uint32_t node_idx = stack[--sp];
                if (node_idx >= shared->static_bvh->node_count) {
                    continue;
                }

                const phys_static_bvh_node_t *n =
                    &shared->static_bvh->nodes[node_idx];
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

                    if (!phys_aabb_overlap(&shared->aabbs[lo], &shared->aabbs[hi])) {
                        continue;
                    }

                    uint32_t slot = atomic_fetch_add_explicit(
                        &shared->atomic_pair_count, 1, memory_order_relaxed);
                    if (slot < shared->max_pairs) {
                        shared->pairs_out[slot].body_a = lo;
                        shared->pairs_out[slot].body_b = hi;
                    }
                } else {
                    if (sp + 2u > BROADPHASE_PAR_STATIC_QUERY_STACK_CAP) {
                        break;
                    }
                    stack[sp++] = n->left;
                    stack[sp++] = n->right;
                }
            }
        }
    }
}

/* ── Active index collection ────────────────────────────────────── */

/**
 * @brief Collect all active tier body indices into a flat array.
 *
 * Iterates all active tiers (ANIM through T4) and appends each body index to out.
 *
 * @param tier_lists  Tier lists to read from.
 * @param out         Output array (must have capacity >= total active).
 * @return Number of indices written.
 */
static uint32_t collect_active_indices(const phys_tier_lists_t *tier_lists,
                                       uint32_t *out) {
    uint32_t n = 0;
    for (int tier = PHYS_TIER_ANIM; tier <= PHYS_TIER_4_BACKGROUND;
         ++tier) {
        const phys_tier_list_t *list = &tier_lists->tiers[tier];
        for (uint32_t i = 0; i < list->count; ++i) {
            out[n++] = list->indices[i];
        }
    }
    return n;
}

/* ── Public API ─────────────────────────────────────────────────── */

void phys_stage_broadphase_par(const phys_broadphase_args_t *args,
                                phys_job_context_t *ctx,
                                phys_frame_arena_t *arena) {
    if (!args || !ctx || !arena) {
        return;
    }
    if (!args->bodies || !args->aabbs || !args->grid ||
        !args->tier_lists || !args->pairs_out || !args->pair_count_out) {
        if (args && args->pair_count_out) {
            *args->pair_count_out = 0;
        }
        return;
    }

    /* Count total active bodies across T0–T4. */
    uint32_t total_active = phys_tier_lists_total_active(args->tier_lists);
    if (total_active == 0) {
        *args->pair_count_out = 0;
        return;
    }

    /* Allocate flat array of active body indices from the arena. */
    uint32_t *active_indices = phys_frame_arena_alloc(
        arena, total_active * sizeof(uint32_t), _Alignof(uint32_t));
    if (!active_indices) {
        /* Fallback to sequential if arena allocation fails. */
        phys_stage_broadphase(args);
        return;
    }

    uint32_t collected = collect_active_indices(args->tier_lists,
                                                active_indices);
    (void)collected; /* Should equal total_active. */

    /* Set up shared context. */
    broadphase_par_shared_t shared = {
        .bodies         = args->bodies,
        .aabbs          = args->aabbs,
        .grid           = args->grid,
        .active_indices = active_indices,

        .static_bvh              = args->static_bvh,
        .static_bucket_flags     = args->static_bucket_flags,
        .static_bucket_flag_count = args->static_bucket_flag_count,

        .pairs_out      = args->pairs_out,
        .max_pairs      = args->max_pairs,
        .atomic_pair_count = 0,
    };

    /* Calculate number of batches. */
    uint32_t batch_size = phys_batch_size(ctx, total_active,
                                          PHYS_BROADPHASE_PAR_BATCH_SIZE, 0);
    uint32_t num_batches = (total_active + batch_size - 1) / batch_size;

    /* Allocate batch descriptors from the arena. */
    phys_job_batch_t *batches = phys_frame_arena_alloc(
        arena, num_batches * sizeof(phys_job_batch_t),
        _Alignof(phys_job_batch_t));
    if (!batches) {
        /* Fallback to sequential. */
        phys_stage_broadphase(args);
        return;
    }

    /* Dispatch all batches. */
    phys_dispatch_stage(ctx, PHYS_STAGE_BROADPHASE,
                        broadphase_par_batch_job, &shared,
                        total_active, batch_size, batches);

    /* Wait for all broadphase jobs to complete. */
    phys_wait_stage(ctx, PHYS_STAGE_BROADPHASE);

    /* Clamp pair count to max_pairs in case of overflow. */
    uint32_t final_count = atomic_load_explicit(
        &shared.atomic_pair_count, memory_order_relaxed);
    if (final_count > args->max_pairs) {
        final_count = args->max_pairs;
    }

    /* ── Halfspace pass (sequential, few halfspaces) ───────────── */
    for (uint32_t h = 0; h < args->halfspace_body_count; ++h) {
        uint32_t hs_body = args->halfspace_bodies[h];

        for (int tier = PHYS_TIER_ANIM; tier <= PHYS_TIER_4_BACKGROUND; ++tier) {
            const phys_tier_list_t *list = &args->tier_lists->tiers[tier];
            for (uint32_t i = 0; i < list->count; ++i) {
                uint32_t dyn = list->indices[i];
                if (dyn == hs_body) continue;

                uint32_t lo = (dyn < hs_body) ? dyn : hs_body;
                uint32_t hi = (dyn < hs_body) ? hs_body : dyn;

                if (final_count < args->max_pairs) {
                    args->pairs_out[final_count].body_a = lo;
                    args->pairs_out[final_count].body_b = hi;
                    final_count++;
                }
            }
        }
    }

    *args->pair_count_out = final_count;
}
