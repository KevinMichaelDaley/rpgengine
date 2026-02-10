#include "ferrum/server/physics/sync/pre_physics_sync.h"

#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/job/counter.h"
#include "ferrum/job/system.h"

/** Per-batch job context (stack-allocated by the caller). */
typedef struct sync_batch_ctx {
    const phys_sync_record_t *records;
    uint32_t start;
    uint32_t count;
    phys_body_pool_t *pool;
} sync_batch_ctx_t;

/** Job function: apply one batch of sync records. */
static void sync_batch_job_(void *user) {
    sync_batch_ctx_t *ctx = (sync_batch_ctx_t *)user;
    const uint32_t end = ctx->start + ctx->count;
    phys_body_pool_t *pool = ctx->pool;

    for (uint32_t i = ctx->start; i < end; ++i) {
        const phys_sync_record_t *r = &ctx->records[i];
        if (!r->dirty) {
            continue;
        }

        phys_body_t *body = phys_body_pool_get_next(pool, r->body_index);
        if (!body) {
            continue;
        }

        body->linear_vel = r->linear_vel;
        body->position = r->position;
        body->entity_index = r->entity_index;
    }
}

#define SYNC_DEFAULT_BATCH 64u
#define SYNC_MAX_BATCHES  256u

int phys_pre_physics_sync_par(const phys_pre_physics_sync_par_args_t *args) {
    if (!args || !args->body_pool || !args->jobs) {
        return -1;
    }
    if (args->record_count == 0) {
        return 0;
    }
    if (!args->records) {
        return -1;
    }

    const uint32_t batch_sz = (args->batch_size > 0) ? args->batch_size
                                                      : SYNC_DEFAULT_BATCH;
    uint32_t num_batches = (args->record_count + batch_sz - 1u) / batch_sz;
    if (num_batches > SYNC_MAX_BATCHES) {
        num_batches = SYNC_MAX_BATCHES;
    }

    /* Stack-allocate batch contexts (bounded by SYNC_MAX_BATCHES). */
    sync_batch_ctx_t ctxs[SYNC_MAX_BATCHES];

    job_counter_t counter;
    job_counter_init(&counter, 0);

    uint32_t dispatched = 0;
    for (uint32_t b = 0; b < num_batches; ++b) {
        uint32_t start = b * batch_sz;
        uint32_t count = batch_sz;
        if (start + count > args->record_count) {
            count = args->record_count - start;
        }

        ctxs[b].records = args->records;
        ctxs[b].start = start;
        ctxs[b].count = count;
        ctxs[b].pool = args->body_pool;

        job_id_t id = job_dispatch_named(args->jobs, sync_batch_job_, &ctxs[b],
                                          0, &counter,
                                          "Srv.Sync.WritingBodies");
        if (id != JOB_ID_INVALID) {
            dispatched++;
        }
    }

    /* Wait for all batches to complete. */
    if (dispatched > 0) {
        (void)job_wait_counter(&counter, 1000u);
    }

    job_counter_destroy(&counter);
    return 0;
}
