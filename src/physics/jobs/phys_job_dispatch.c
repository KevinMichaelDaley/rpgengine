/**
 * @file phys_job_dispatch.c
 * @brief Physics job dispatch — maps pipeline stages to batched engine jobs.
 *
 * Provides init/destroy for the physics job context and dispatch/wait
 * helpers that split work into batches and use named dispatch for Tracy
 * instrumentation.
 */

#include "ferrum/physics/phys_jobs.h"

#include "ferrum/job/instrumentation.h"
#include "ferrum/job/system.h"

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

/* Job-system instrumentation event names.
   Using pre-baked strings avoids allocations and gives us a clear "last stage"
   breadcrumb when the physics tick appears to freeze. */
static const char *stage_dispatch_ev[PHYS_STAGE_COUNT] = {
    "phys_dispatch:step_plan",
    "phys_dispatch:tier_classify",
    "phys_dispatch:spatial_update",
    "phys_dispatch:halo_closure",
    "phys_dispatch:aabb_update",
    "phys_dispatch:broadphase",
    "phys_dispatch:narrowphase",
    "phys_dispatch:manifold_build",
    "phys_dispatch:stabilization",
    "phys_dispatch:constraint_build",
    "phys_dispatch:island_build",
    "phys_dispatch:tgs_solve",
    "phys_dispatch:xpbd_solve",
    "phys_dispatch:integrate",
    "phys_dispatch:cache_commit",
};

static const char *stage_wait_begin_ev[PHYS_STAGE_COUNT] = {
    "phys_wait_begin:step_plan",
    "phys_wait_begin:tier_classify",
    "phys_wait_begin:spatial_update",
    "phys_wait_begin:halo_closure",
    "phys_wait_begin:aabb_update",
    "phys_wait_begin:broadphase",
    "phys_wait_begin:narrowphase",
    "phys_wait_begin:manifold_build",
    "phys_wait_begin:stabilization",
    "phys_wait_begin:constraint_build",
    "phys_wait_begin:island_build",
    "phys_wait_begin:tgs_solve",
    "phys_wait_begin:xpbd_solve",
    "phys_wait_begin:integrate",
    "phys_wait_begin:cache_commit",
};

static const char *stage_wait_end_ev[PHYS_STAGE_COUNT] = {
    "phys_wait_end:step_plan",
    "phys_wait_end:tier_classify",
    "phys_wait_end:spatial_update",
    "phys_wait_end:halo_closure",
    "phys_wait_end:aabb_update",
    "phys_wait_end:broadphase",
    "phys_wait_end:narrowphase",
    "phys_wait_end:manifold_build",
    "phys_wait_end:stabilization",
    "phys_wait_end:constraint_build",
    "phys_wait_end:island_build",
    "phys_wait_end:tgs_solve",
    "phys_wait_end:xpbd_solve",
    "phys_wait_end:integrate",
    "phys_wait_end:cache_commit",
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

    /* Fast path: single batch — run inline to avoid dispatch/wait overhead
       and eliminate any fiber-scheduling races for the trivial case. */
    if (num_jobs == 1) {
        batches[0].user_args = user_args;
        batches[0].start     = 0;
        batches[0].count     = total_items;
        batches[0].batch_idx = 0;
        fn(&batches[0]);
        return 0; /* Return 0 so caller knows no wait is needed. */
    }

    job_instrument_event(stage_dispatch_ev[stage], 0, 0, job_current_worker_id(), __FILE__, __LINE__);

    /* Reset the counter for this stage.  Initialize to 0 because
       job_dispatch_named() auto-increments the counter for each job. */
    job_counter_destroy(&ctx->counters[stage]);
    job_counter_init(&ctx->counters[stage], 0);

    /* Fill batch descriptors and dispatch each job. */
    uint32_t remaining = total_items;
    for (uint32_t j = 0; j < num_jobs; j++) {
        uint32_t count = (remaining < batch_size) ? remaining : batch_size;
        batches[j].user_args  = user_args;
        batches[j].start      = j * batch_size;
        batches[j].count      = count;
        batches[j].batch_idx  = j;

        job_dispatch_named(ctx->job_sys, fn, &batches[j], 0,
                           &ctx->counters[stage], stage_names[stage]);

        remaining -= count;
    }

    return num_jobs;
}

void phys_wait_stage(phys_job_context_t *ctx, phys_stage_id_t stage) {
    job_instrument_event(stage_wait_begin_ev[stage], 0, 0, job_current_worker_id(), __FILE__, __LINE__);
    job_wait_counter(&ctx->counters[stage], 128);
    job_instrument_event(stage_wait_end_ev[stage], 0, 0, job_current_worker_id(), __FILE__, __LINE__);
}
