/**
 * @file spatial_update_par.c
 * @brief Parallel spatial update stage implementation.
 *
 * Two-phase approach:
 *   Phase A (parallel): each job computes AABBs for a disjoint range of bodies.
 *   Phase B (sequential): insert all active bodies into the spatial grid.
 *
 * Grid insertion is sequential because phys_spatial_grid_insert uses
 * arena allocation (phys_frame_arena_alloc) which is not thread-safe.
 */

#include "ferrum/physics/par/spatial_update_par.h"

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/spatial_grid.h"

#include <stddef.h>

/* ── Maximum batch count ────────────────────────────────────────── */

/**
 * @brief Maximum number of batches we support.
 *
 * With 512 bodies/batch and 64 batches, supports up to 32768 bodies.
 * Increase if needed for larger worlds.
 */
#define MAX_SPATIAL_BATCHES 64u

/* ── Phase A job function ───────────────────────────────────────── */

/**
 * @brief Compute AABBs for a range of bodies (one parallel batch).
 *
 * Each job writes to disjoint aabbs_out[start..start+count-1], so
 * no synchronization is needed between jobs.
 *
 * @param data  Pointer to a phys_job_batch_t whose user_args points
 *              to a const phys_spatial_update_args_t.
 */
static void spatial_aabb_job(void *data) {
    phys_job_batch_t *batch = data;
    const phys_spatial_update_args_t *args = batch->user_args;

    uint32_t start = batch->start;
    uint32_t end   = start + batch->count;

    for (uint32_t i = start; i < end; ++i) {
        /* Skip inactive bodies when an active mask is provided. */
        if (args->active && !args->active[i]) {
            continue;
        }

        const phys_body_t *body       = &args->bodies[i];
        const phys_collider_t *collider = &args->colliders[i];
        phys_aabb_t *aabb             = &args->aabbs_out[i];

        /* Compute world-space center and rotation for this collider. */
        phys_vec3_t center = phys_collider_world_center(
            collider, body->position, body->orientation);
        phys_quat_t rotation = phys_collider_world_rotation(
            collider, body->orientation);

        /* Build the AABB based on the collider's shape type. */
        switch (collider->type) {
            case PHYS_SHAPE_SPHERE: {
                float radius = args->spheres[collider->shape_index].radius;
                phys_aabb_from_sphere(aabb, center, radius);
                break;
            }
            case PHYS_SHAPE_BOX: {
                phys_vec3_t half_extents =
                    args->boxes[collider->shape_index].half_extents;
                phys_aabb_from_box(aabb, center, rotation, half_extents);
                break;
            }
            case PHYS_SHAPE_CAPSULE: {
                const phys_capsule_t *cap =
                    &args->capsules[collider->shape_index];
                phys_aabb_from_capsule(
                    aabb, center, rotation, cap->radius, cap->half_height);
                break;
            }
            default:
                /* Unknown shape — produce a zero-volume AABB at the center. */
                *aabb = (phys_aabb_t){center, center};
                break;
        }
    }
}

/* ── Phase B: sequential grid insertion ─────────────────────────── */

/**
 * @brief Insert all active bodies into the spatial grid (sequential).
 *
 * Must be called after all AABB jobs have completed so that aabbs_out
 * is fully populated.
 */
static void spatial_grid_insert_all(const phys_spatial_update_args_t *args) {
    for (uint32_t i = 0; i < args->body_count; ++i) {
        if (args->active && !args->active[i]) {
            continue;
        }
        phys_spatial_grid_insert(args->grid_out, i, &args->aabbs_out[i]);
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void phys_stage_spatial_update_par(const phys_spatial_update_args_t *args,
                                    phys_job_context_t *ctx) {
    if (!args) {
        return;
    }

    /* Fall back to sequential if no job context is provided. */
    if (!ctx) {
        phys_stage_spatial_update(args);
        return;
    }

    /* Clear the grid before computing AABBs. */
    phys_spatial_grid_clear(args->grid_out);

    if (args->body_count == 0) {
        return;
    }

    /* Phase A: dispatch parallel AABB computation. */
    phys_job_batch_t batches[MAX_SPATIAL_BATCHES];
    uint32_t num_jobs = phys_dispatch_stage(
        ctx,
        PHYS_STAGE_SPATIAL_UPDATE,
        spatial_aabb_job,
        (void *)(uintptr_t)args, /* cast away const for user_args void* */
        args->body_count,
        PHYS_SPATIAL_UPDATE_BATCH_SIZE,
        batches);

    /* Wait for all AABB jobs to finish. */
    if (num_jobs > 0) {
        phys_wait_stage(ctx, PHYS_STAGE_SPATIAL_UPDATE);
    }

    /* Phase B: sequential grid insertion. */
    spatial_grid_insert_all(args);
}
