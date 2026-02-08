/**
 * @file constraint_build_par.c
 * @brief Parallel constraint build stage implementation.
 *
 * Each job processes a contiguous range of manifolds and atomically
 * claims output slots in the shared constraint array via fetch-add
 * on an atomic counter.  This avoids contention on the output buffer
 * while allowing variable-length output per manifold.
 */

#include "ferrum/physics/par/constraint_build_par.h"

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/stabilization.h"
#include "ferrum/physics/step_plan.h"

#include <math.h>
#include <stdatomic.h>
#include <stddef.h>

/* ── Maximum batch count ────────────────────────────────────────── */

/**
 * @brief Maximum number of batches we support.
 *
 * With 32 manifolds/batch and 128 batches, supports up to 4096 manifolds.
 */

/* ── Shared context for parallel jobs ───────────────────────────── */

/**
 * @brief Internal context shared across all constraint build jobs.
 *
 * Wraps the original args plus an atomic counter for claiming
 * output constraint slots.
 */
typedef struct constraint_build_par_ctx {
    const phys_constraint_build_args_t *args;  /**< Original stage args.         */
    atomic_uint                         out_idx; /**< Atomic output write index. */
} constraint_build_par_ctx_t;

/* ── Job function ───────────────────────────────────────────────── */

/**
 * @brief Process a range of manifolds and build constraints (one batch).
 *
 * For each manifold in [start..start+count), counts the contact points
 * that fit within max_constraints, atomically claims that many output
 * slots, then fills them.
 *
 * @param data  Pointer to a phys_job_batch_t whose user_args points
 *              to a constraint_build_par_ctx_t.
 */
static void constraint_build_job(void *data) {
    phys_job_batch_t *batch = data;
    constraint_build_par_ctx_t *par_ctx = batch->user_args;
    const phys_constraint_build_args_t *args = par_ctx->args;

    uint32_t m_start = batch->start;
    uint32_t m_end   = m_start + batch->count;

    /* First pass: count how many constraints this batch will produce. */
    uint32_t local_count = 0;
    for (uint32_t m = m_start; m < m_end; ++m) {
        local_count += args->manifolds[m].point_count;
    }

    if (local_count == 0) {
        return;
    }

    /* Atomically claim output slots. */
    uint32_t base = atomic_fetch_add(&par_ctx->out_idx, local_count);

    /* Respect max_constraints: if we've overshot, clamp. */
    if (base >= args->max_constraints) {
        /* No room at all — give back our claim by not writing anything.
         * The final count is taken from out_idx, clamped later. */
        return;
    }

    uint32_t avail = args->max_constraints - base;
    if (local_count > avail) {
        local_count = avail;
    }

    /* Second pass: build constraints into the claimed slots. */
    uint32_t written = 0;
    for (uint32_t m = m_start; m < m_end && written < local_count; ++m) {
        const phys_manifold_t  *manifold = &args->manifolds[m];
        const phys_stab_hint_t *hint     = &args->hints[m];
        const phys_body_t      *body_a   = &args->bodies[manifold->body_a];
        const phys_body_t      *body_b   = &args->bodies[manifold->body_b];

        /* Apply stabilization hints to material properties. */
        float friction    = manifold->friction    * hint->friction_scale;
        float restitution = manifold->restitution * hint->restitution_scale;

        for (uint8_t p = 0; p < manifold->point_count && written < local_count; ++p) {
            phys_constraint_t *c = &args->constraints_out[base + written];
            written++;

            /* Build Jacobian rows (normal + 2 friction tangent). */
            phys_constraint_build_contact(c, body_a, body_b,
                                          &manifold->points[p],
                                          friction, restitution,
                                          args->dt, args->baumgarte,
                                          args->slop);

            /* Back-references for solver writeback. */
            c->body_a       = manifold->body_a;
            c->body_b       = manifold->body_b;
            c->manifold_idx = m;
            c->point_idx    = (uint8_t)p;

            /* Determine solver mode from body tiers. */
            c->solver_mode = (uint8_t)phys_tier_cross_solver_mode(
                (phys_tier_t)body_a->tier, (phys_tier_t)body_b->tier);

            /* Load warmstart impulses from manifold cache.
             * Sanitize NaN/Inf to prevent solver corruption. */
            c->rows[0].lambda = manifold->normal_impulse[p];
            c->rows[1].lambda = manifold->tangent_impulse[p][0];
            c->rows[2].lambda = manifold->tangent_impulse[p][1];
            for (uint8_t r = 0; r < 3; ++r) {
                if (isnan(c->rows[r].lambda) || isinf(c->rows[r].lambda)) {
                    c->rows[r].lambda = 0.0f;
                }
            }
        }
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void phys_stage_constraint_build_par(const phys_constraint_build_args_t *args,
                                      phys_job_context_t *ctx,
                                      phys_frame_arena_t *arena) {
    if (!args) {
        return;
    }

    /* Fall back to sequential if no job context is provided. */
    if (!ctx || !arena) {
        phys_stage_constraint_build(args);
        return;
    }

    /* Validate required pointers, matching sequential no-op behavior. */
    if (!args->constraints_out || !args->constraint_count_out) {
        return;
    }

    if (args->manifold_count == 0) {
        *args->constraint_count_out = 0;
        return;
    }

    if (!args->manifolds || !args->hints || !args->bodies) {
        return;
    }

    /* Set up shared parallel context with atomic output index. */
    constraint_build_par_ctx_t par_ctx;
    par_ctx.args = args;
    atomic_init(&par_ctx.out_idx, 0);

    /* Allocate batch descriptors from the frame arena. */
    uint32_t num_batches = (args->manifold_count + PHYS_CONSTRAINT_BUILD_BATCH_SIZE - 1)
                           / PHYS_CONSTRAINT_BUILD_BATCH_SIZE;
    phys_job_batch_t *batches = phys_frame_arena_alloc(
        arena, num_batches * sizeof(phys_job_batch_t),
        _Alignof(phys_job_batch_t));
    if (!batches) {
        phys_stage_constraint_build(args);
        return;
    }

    uint32_t num_jobs = phys_dispatch_stage(
        ctx,
        PHYS_STAGE_CONSTRAINT_BUILD,
        constraint_build_job,
        &par_ctx,
        args->manifold_count,
        PHYS_CONSTRAINT_BUILD_BATCH_SIZE,
        batches);

    /* Wait for all jobs to finish. */
    if (num_jobs > 0) {
        phys_wait_stage(ctx, PHYS_STAGE_CONSTRAINT_BUILD);
    }

    /* Write final constraint count, clamped to max_constraints. */
    uint32_t total = atomic_load(&par_ctx.out_idx);
    if (total > args->max_constraints) {
        total = args->max_constraints;
    }
    *args->constraint_count_out = total;
}
