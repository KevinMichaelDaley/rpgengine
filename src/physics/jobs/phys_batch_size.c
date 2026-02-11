/**
 * @file phys_batch_size.c
 * @brief Dynamic batch size computation for physics job dispatch.
 *
 * Targets ~(2 * worker_count) jobs per dispatch to balance parallelism
 * against dispatch overhead.
 */

#include "ferrum/physics/phys_jobs.h"

uint32_t phys_batch_size(const phys_job_context_t *ctx,
                         uint32_t total_items,
                         uint32_t min_batch,
                         uint32_t max_batch) {
    if (!ctx || !ctx->job_sys || total_items == 0) {
        return min_batch > 0 ? min_batch : 1;
    }
    if (min_batch == 0) min_batch = 1;
    if (max_batch == 0) max_batch = total_items;
    if (max_batch < min_batch) max_batch = min_batch;

    /* Target 2× worker count jobs — enough to hide scheduling jitter
     * while keeping dispatch overhead low. */
    uint32_t workers = ctx->job_sys->worker_count;
    uint32_t target_jobs = workers * 2;
    if (target_jobs < 2) {
        target_jobs = 2;
    }

    /* Compute batch size: ceil(total_items / target_jobs). */
    uint32_t bs = (total_items + target_jobs - 1) / target_jobs;

    /* Clamp to [min_batch, max_batch]. */
    if (bs < min_batch) {
        bs = min_batch;
    }
    if (bs > max_batch) {
        bs = max_batch;
    }

    return bs;
}
