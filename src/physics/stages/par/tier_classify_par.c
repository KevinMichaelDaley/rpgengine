/**
 * @file tier_classify_par.c
 * @brief Parallel tier classification implementation.
 *
 * Splits body range into batches of PHYS_TIER_CLASSIFY_BATCH_SIZE,
 * dispatches each batch as a job that classifies into a thread-local
 * tier_lists, then merges all per-batch results into the final output.
 *
 * Non-static functions: 1 (phys_stage_tier_classify_par).
 */

#include "ferrum/physics/par/tier_classify_par.h"

#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/tier_list.h"

/* ── Per-batch context ──────────────────────────────────────────── */

/**
 * @brief Shared context across all batches in a parallel tier classify.
 *
 * Each job receives a phys_job_batch_t whose user_args points here.
 * The per_batch_lists array has one entry per batch; each batch job
 * writes exclusively to its own tier lists (no contention).
 */
typedef struct tier_classify_shared {
    const phys_body_t *bodies;           /**< Body array (read-only). */
    const uint8_t     *active;           /**< Per-slot activity flags. */
    phys_tier_lists_t *per_batch_lists;  /**< Array of tier lists, one per batch. */
} tier_classify_shared_t;

/* ── Job function ───────────────────────────────────────────────── */

/**
 * @brief Job function: classify bodies in [start, start+count) into
 *        the batch's local tier lists.
 */
static void tier_classify_batch_job(void *data) {
    phys_job_batch_t *batch = data;
    tier_classify_shared_t *shared = batch->user_args;

    /* Determine which batch index this is. */
    uint32_t batch_index = batch->batch_idx;
    phys_tier_lists_t *local_lists = &shared->per_batch_lists[batch_index];

    uint32_t end = batch->start + batch->count;
    for (uint32_t i = batch->start; i < end; ++i) {
        /* Skip inactive pool slots. */
        if (shared->active && !shared->active[i]) {
            continue;
        }

        const phys_body_t *body = &shared->bodies[i];

        /* Static bodies are excluded from all tier lists. */
        if (phys_body_is_static(body)) {
            continue;
        }

        if (phys_body_is_sleeping(body)) {
            phys_tier_list_add(
                &local_lists->tiers[PHYS_TIER_5_SLEEPING], i);
        } else if (body->tier == PHYS_TIER_ANIM) {
            /* Animated bodies stay in ANIM tier. */
            phys_tier_list_add(
                &local_lists->tiers[PHYS_TIER_ANIM], i);
        } else {
            /* Phase 1: all non-sleeping dynamic bodies → T0. */
            phys_tier_list_add(
                &local_lists->tiers[PHYS_TIER_0_DIRECT], i);
        }
    }
}

/* ── Merge helper ───────────────────────────────────────────────── */

/**
 * @brief Merge per-batch tier lists into the final output tier lists.
 *
 * Appends all indices from each batch's tier lists into the output.
 */
static void merge_tier_lists(phys_tier_lists_t *out,
                             const phys_tier_lists_t *per_batch,
                             uint32_t num_batches) {
    for (uint32_t b = 0; b < num_batches; ++b) {
        for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
            const phys_tier_list_t *src = &per_batch[b].tiers[t];
            phys_tier_list_t *dst = &out->tiers[t];
            for (uint32_t j = 0; j < src->count; ++j) {
                phys_tier_list_add(dst, src->indices[j]);
            }
        }
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void phys_stage_tier_classify_par(const phys_tier_classify_args_t *args,
                                   phys_job_context_t *ctx) {
    if (!args || !ctx || !args->tier_lists_out || !args->arena) {
        return;
    }

    uint32_t body_count = args->body_count;

    /* Initialize the output tier lists from the arena. */
    phys_tier_lists_init(args->tier_lists_out, args->arena, body_count);

    if (body_count == 0) {
        return;
    }

    /* Calculate dynamic batch size targeting 2× worker count jobs. */
    uint32_t batch_size = phys_batch_size(ctx, body_count,
                                          PHYS_TIER_CLASSIFY_BATCH_SIZE, 0);
    uint32_t num_batches = (body_count + batch_size - 1) / batch_size;

    /* Allocate per-batch tier lists from the arena. */
    phys_tier_lists_t *per_batch = phys_frame_arena_alloc(
        args->arena,
        num_batches * sizeof(phys_tier_lists_t),
        _Alignof(phys_tier_lists_t));
    if (!per_batch) {
        /* Fallback to sequential if arena allocation fails. */
        phys_stage_tier_classify(args);
        return;
    }

    /* Initialize each per-batch tier list set from the arena.
     * Each batch can hold at most batch_size bodies. */
    for (uint32_t b = 0; b < num_batches; ++b) {
        uint32_t this_batch_count = batch_size;
        if (b == num_batches - 1) {
            this_batch_count = body_count - b * batch_size;
        }
        phys_tier_lists_init(&per_batch[b], args->arena, this_batch_count);
    }

    /* Set up shared context. */
    tier_classify_shared_t shared = {
        .bodies          = args->bodies,
        .active          = args->active,
        .per_batch_lists = per_batch,
    };

    /* Allocate batch descriptors from the arena. */
    phys_job_batch_t *batches = phys_frame_arena_alloc(
        args->arena,
        num_batches * sizeof(phys_job_batch_t),
        _Alignof(phys_job_batch_t));
    if (!batches) {
        /* Fallback to sequential. */
        phys_stage_tier_classify(args);
        return;
    }

    /* Dispatch all batches. */
    phys_dispatch_stage(ctx, PHYS_STAGE_TIER_CLASSIFY,
                        tier_classify_batch_job, &shared,
                        body_count, batch_size, batches);

    /* Wait for all classification jobs to complete. */
    phys_wait_stage(ctx, PHYS_STAGE_TIER_CLASSIFY);

    /* Merge per-batch results into the final output. */
    merge_tier_lists(args->tier_lists_out, per_batch, num_batches);
}
