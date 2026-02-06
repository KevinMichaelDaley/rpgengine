/**
 * @file phys_job_dispatch.c
 * @brief Physics job dispatch — maps pipeline stages to batched engine jobs.
 *
 * Provides init/destroy for the physics job context and dispatch/wait
 * helpers that split work into batches and use named dispatch for Tracy
 * instrumentation.
 */

#include "ferrum/physics/phys_jobs.h"

#include <stddef.h>
#include <string.h>

/* Tracy-friendly stage names indexed by phys_stage_id_t. */
static const char *stage_names[PHYS_STAGE_COUNT] = {
    "phys:step_plan",
    "phys:tier_classify",
    "phys:spatial_update",
    "phys:halo_closure",
    "phys:aabb_update",
    "phys:broadphase",
    "phys:narrowphase",
    "phys:manifold_build",
    "phys:stabilization",
    "phys:constraint_build",
    "phys:island_build",
    "phys:tgs_solve",
    "phys:xpbd_solve",
    "phys:integrate",
    "phys:cache_commit",
};

/* ── Public API (4 non-static functions) ────────────────────────── */

void phys_job_context_init(phys_job_context_t *ctx, job_system_t *sys) {
    if (!ctx || !sys) {
        return;
    }
    ctx->job_sys = sys;
    for (uint32_t i = 0; i < PHYS_STAGE_COUNT; i++) {
        job_counter_init(&ctx->counters[i], 0);
    }
}

void phys_job_context_destroy(phys_job_context_t *ctx) {
    if (!ctx) {
        return;
    }
    for (uint32_t i = 0; i < PHYS_STAGE_COUNT; i++) {
        job_counter_destroy(&ctx->counters[i]);
    }
    ctx->job_sys = NULL;
}

uint32_t phys_dispatch_stage(phys_job_context_t *ctx,
                             phys_stage_id_t stage,
                             void (*fn)(void *user_data),
                             void *user_args,
                             uint32_t total_items,
                             uint32_t batch_size,
                             phys_job_batch_t *batches) {
    if (total_items == 0) {
        return 0;
    }

    /* Number of batches: ceil(total_items / batch_size). */
    uint32_t num_jobs = (total_items + batch_size - 1) / batch_size;

    /* Reset the counter for this stage.  Initialize to 0 because
       job_dispatch_named() auto-increments the counter for each job. */
    job_counter_destroy(&ctx->counters[stage]);
    job_counter_init(&ctx->counters[stage], 0);

    /* Fill batch descriptors and dispatch each job. */
    uint32_t remaining = total_items;
    for (uint32_t j = 0; j < num_jobs; j++) {
        uint32_t count = (remaining < batch_size) ? remaining : batch_size;
        batches[j].user_args = user_args;
        batches[j].start     = j * batch_size;
        batches[j].count     = count;

        job_dispatch_named(ctx->job_sys, fn, &batches[j], 0,
                           &ctx->counters[stage], stage_names[stage]);

        remaining -= count;
    }

    return num_jobs;
}

void phys_wait_stage(phys_job_context_t *ctx, phys_stage_id_t stage) {
    job_wait_counter(&ctx->counters[stage], 128);
}
