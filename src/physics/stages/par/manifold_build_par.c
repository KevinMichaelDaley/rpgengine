/**
 * @file manifold_build_par.c
 * @brief Parallel manifold build implementation.
 *
 * Splits contact candidates into batches of PHYS_MANIFOLD_BUILD_BATCH_SIZE
 * and dispatches each batch as a job. Manifold cache access is protected
 * by a mutex; output slot allocation uses atomic_fetch_add.
 *
 * Non-static functions: 1 (phys_stage_manifold_build_par).
 */

#include "ferrum/physics/par/manifold_build_par.h"

#include <stdatomic.h>
#include <string.h>
#include <threads.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/narrowphase.h"

/* ── Shared context ─────────────────────────────────────────────── */

/**
 * @brief Shared context across all batches in a parallel manifold build.
 *
 * Each job receives a phys_job_batch_t whose user_args points here.
 * The mutex protects cache access; the atomic output_index is used to
 * claim slots in the output buffer without contention.
 */
typedef struct manifold_build_shared {
    const phys_contact_candidate_t *candidates; /**< Candidate array (read-only). */
    struct phys_manifold_cache *cache;           /**< Shared manifold cache (mutex-protected). */
    phys_manifold_t *manifolds_out;              /**< Output buffer for manifolds. */
    uint32_t max_manifolds;                      /**< Capacity of manifolds_out. */
    uint64_t tick;                               /**< Current simulation tick. */
    const phys_body_t *bodies;                   /**< Body array for material lookups. */
    mtx_t cache_mtx;                             /**< Mutex protecting cache access. */
    atomic_uint output_index;                    /**< Atomic counter for output slot allocation. */
} manifold_build_shared_t;

/* ── Job function ───────────────────────────────────────────────── */

/**
 * @brief Process candidates [start, start+count) for manifold build.
 *
 * For each candidate in the range:
 *   1. Lock the cache mutex and get/create the cached manifold.
 *   2. Save old impulses for warmstart matching.
 *   3. Clear and rebuild the manifold from the candidate.
 *   4. Restore matched impulses.
 *   5. Copy to output (unlock cache first, then claim slot atomically).
 */
static void manifold_build_batch_job(void *data) {
    phys_job_batch_t *batch = data;
    manifold_build_shared_t *shared = batch->user_args;

    uint32_t end = batch->start + batch->count;
    for (uint32_t i = batch->start; i < end; ++i) {
        const phys_contact_candidate_t *cand = &shared->candidates[i];

        /* Lock cache for get_or_create + warmstart save + rebuild. */
        mtx_lock(&shared->cache_mtx);

        /* 1. Get or create cached manifold for this body pair. */
        phys_manifold_t *cached = phys_manifold_cache_get_or_create(
            shared->cache, cand->body_a, cand->body_b,
            (uint32_t)shared->tick);
        if (!cached) {
            mtx_unlock(&shared->cache_mtx);
            continue; /* cache full */
        }

        /* 2. Save old impulses and feature IDs for warmstart matching. */
        float old_normal[PHYS_MAX_MANIFOLD_POINTS];
        float old_tangent[PHYS_MAX_MANIFOLD_POINTS][2];
        uint32_t old_features[PHYS_MAX_MANIFOLD_POINTS];
        uint8_t old_count = cached->point_count;

        for (uint8_t j = 0; j < old_count; ++j) {
            old_normal[j]     = cached->normal_impulse[j];
            old_tangent[j][0] = cached->tangent_impulse[j][0];
            old_tangent[j][1] = cached->tangent_impulse[j][1];
            old_features[j]   = cached->points[j].feature_id;
        }

        /* 3. Clear manifold, zero impulses, and set body IDs. */
        phys_manifold_clear(cached);
        for (uint8_t j = 0; j < PHYS_MAX_MANIFOLD_POINTS; ++j) {
            cached->normal_impulse[j]     = 0.0f;
            cached->tangent_impulse[j][0] = 0.0f;
            cached->tangent_impulse[j][1] = 0.0f;
        }
        cached->body_a = cand->body_a;
        cached->body_b = cand->body_b;

        /* Combine surface material from both bodies. */
        if (shared->bodies) {
            cached->friction = phys_combine_friction(
                shared->bodies[cand->body_a].friction,
                shared->bodies[cand->body_b].friction);
            cached->restitution = phys_combine_restitution(
                shared->bodies[cand->body_a].restitution,
                shared->bodies[cand->body_b].restitution);
        }

        /* 4. Add new contact points from the candidate. */
        for (uint8_t j = 0; j < cand->contact_count; ++j) {
            phys_manifold_add_point(cached, &cand->contacts[j]);
        }

        /* 5. Match new contacts to old by feature_id → restore impulses. */
        for (uint8_t j = 0; j < cached->point_count; ++j) {
            uint32_t feat = cached->points[j].feature_id;
            for (uint8_t k = 0; k < old_count; ++k) {
                if (old_features[k] == feat) {
                    cached->normal_impulse[j]     = old_normal[k];
                    cached->tangent_impulse[j][0]  = old_tangent[k][0];
                    cached->tangent_impulse[j][1]  = old_tangent[k][1];
                    break;
                }
            }
        }

        /* Take a local copy of the manifold before unlocking. */
        phys_manifold_t local_copy = *cached;

        mtx_unlock(&shared->cache_mtx);

        /* 6. Atomically claim an output slot and copy. */
        uint32_t slot = atomic_fetch_add(&shared->output_index, 1);
        if (slot < shared->max_manifolds) {
            shared->manifolds_out[slot] = local_copy;
        }
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void phys_stage_manifold_build_par(const phys_manifold_build_args_t *args,
                                    phys_job_context_t *ctx,
                                    phys_frame_arena_t *arena) {
    if (!args || !ctx || !arena) {
        if (args && args->manifold_count_out) {
            *args->manifold_count_out = 0;
        }
        return;
    }

    if (!args->candidates || !args->cache
        || !args->manifolds_out || !args->manifold_count_out) {
        if (args->manifold_count_out) {
            *args->manifold_count_out = 0;
        }
        return;
    }

    if (args->candidate_count == 0) {
        *args->manifold_count_out = 0;
        return;
    }

    /* Set up shared context. */
    manifold_build_shared_t shared = {
        .candidates    = args->candidates,
        .cache         = args->cache,
        .manifolds_out = args->manifolds_out,
        .max_manifolds = args->max_manifolds,
        .tick          = args->tick,
        .bodies        = args->bodies,
    };
    mtx_init(&shared.cache_mtx, mtx_plain);
    atomic_store(&shared.output_index, 0);

    /* Calculate batch count. */
    uint32_t batch_size  = PHYS_MANIFOLD_BUILD_BATCH_SIZE;
    uint32_t num_batches = (args->candidate_count + batch_size - 1) / batch_size;

    /* Allocate batch descriptors from the frame arena. */
    phys_job_batch_t *batches = phys_frame_arena_alloc(
        arena, num_batches * sizeof(phys_job_batch_t),
        _Alignof(phys_job_batch_t));
    if (!batches) {
        /* Fallback to sequential if arena allocation fails. */
        mtx_destroy(&shared.cache_mtx);
        phys_stage_manifold_build(args);
        return;
    }

    /* Dispatch all batches. */
    phys_dispatch_stage(ctx, PHYS_STAGE_MANIFOLD_BUILD,
                        manifold_build_batch_job, &shared,
                        args->candidate_count, batch_size, batches);

    /* Wait for all jobs to complete. */
    phys_wait_stage(ctx, PHYS_STAGE_MANIFOLD_BUILD);

    /* Write final manifold count, clamped to max_manifolds. */
    uint32_t final_count = atomic_load(&shared.output_index);
    if (final_count > args->max_manifolds) {
        final_count = args->max_manifolds;
    }
    *args->manifold_count_out = final_count;

    mtx_destroy(&shared.cache_mtx);
}
