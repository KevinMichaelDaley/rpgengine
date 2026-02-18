/**
 * @file narrowphase_par.c
 * @brief Parallel narrowphase dispatch.
 *
 * Splits broadphase pairs into batches of 64 and dispatches each
 * batch as a job.  Each job iterates its pair slice, performs
 * shape-specific collision tests, and uses atomic fetch-add on a
 * shared counter to claim output slots in the candidate buffer.
 */

#include <stdatomic.h>
#include <string.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/par/narrowphase_par.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/mesh_narrowphase.h"
#include "ferrum/physics/narrowphase_convex.h"
#include "ferrum/physics/convex_compound.h"
#include "ferrum/physics/collision/box_box.h"
#include "ferrum/physics/collision/box_capsule.h"
#include "ferrum/physics/collision/capsule_capsule.h"
#include "ferrum/physics/collision/halfspace.h"

/** Batch size: number of broadphase pairs per job. */
#define NP_PAR_BATCH_SIZE 64

/* ── Internal shared state ─────────────────────────────────────── */

/**
 * @brief Shared context passed to each narrowphase job.
 *
 * Wraps the original narrowphase args plus an atomic output index
 * so that jobs can safely claim candidate slots without locking.
 */
typedef struct np_par_shared {
    const phys_narrowphase_args_t *args;  /**< Original narrowphase args.    */
    atomic_uint                    out_idx; /**< Next free candidate slot.   */
} np_par_shared_t;

/* ── Job function ──────────────────────────────────────────────── */

/**
 * @brief Process a batch of broadphase pairs.
 *
 * Iterates pairs [batch->start .. batch->start + batch->count),
 * performs shape-specific intersection tests, and writes contact
 * candidates to the shared output buffer using atomic_fetch_add
 * to claim slots.
 *
 * @param user_data  Pointer to a phys_job_batch_t whose user_args
 *                   points to an np_par_shared_t.
 */
static void np_par_job_fn(void *user_data)
{
    phys_job_batch_t *batch = user_data;
    np_par_shared_t *shared = batch->user_args;
    const phys_narrowphase_args_t *args = shared->args;

    uint32_t end = batch->start + batch->count;
    if (end > args->pair_count) {
        end = args->pair_count;
    }

    for (uint32_t i = batch->start; i < end; i++) {
        uint32_t a = args->pairs[i].body_a;
        uint32_t b = args->pairs[i].body_b;

        const phys_collider_t *ca = &args->colliders[a];
        const phys_collider_t *cb = &args->colliders[b];

        /* Compute world-space transforms for both bodies. */
        phys_vec3_t wa = phys_collider_world_center(
            ca, args->bodies[a].position, args->bodies[a].orientation);
        phys_quat_t qa = phys_collider_world_rotation(
            ca, args->bodies[a].orientation);

        phys_vec3_t wb = phys_collider_world_center(
            cb, args->bodies[b].position, args->bodies[b].orientation);
        phys_quat_t qb = phys_collider_world_rotation(
            cb, args->bodies[b].orientation);

        /* Normalize the pair so that type_a <= type_b. */
        uint32_t ba = a, bb = b;
        const phys_collider_t *c0 = ca, *c1 = cb;
        phys_vec3_t w0 = wa, w1 = wb;
        phys_quat_t q0 = qa, q1 = qb;
        if (c0->type > c1->type) {
            ba = b; bb = a;
            c0 = cb; c1 = ca;
            w0 = wb; w1 = wa;
            q0 = qb; q1 = qa;
        }

        phys_contact_point_t contact;
        memset(&contact, 0, sizeof(contact));
        bool hit = false;

        /* ── Dispatch on normalized (type_lo, type_hi) pair ─────── */
        if (c0->type == PHYS_SHAPE_SPHERE && c1->type == PHYS_SHAPE_SPHERE) {
            float r0 = args->spheres[c0->shape_index].radius;
            float r1 = args->spheres[c1->shape_index].radius;
            hit = phys_sphere_vs_sphere(w0, r0, w1, r1,
                                        args->speculative_margin, &contact);
        }
        else if (c0->type == PHYS_SHAPE_SPHERE && c1->type == PHYS_SHAPE_BOX) {
            float rs = args->spheres[c0->shape_index].radius;
            phys_vec3_t he = args->boxes[c1->shape_index].half_extents;
            hit = phys_sphere_vs_box(w0, rs, w1, q1, he,
                                    args->speculative_margin, &contact);
        }
        else if (c0->type == PHYS_SHAPE_SPHERE && c1->type == PHYS_SHAPE_CAPSULE) {
            float rs = args->spheres[c0->shape_index].radius;
            float rc = args->capsules[c1->shape_index].radius;
            float hh = args->capsules[c1->shape_index].half_height;
            hit = phys_sphere_vs_capsule(w0, rs, w1, q1, rc, hh,
                                        args->speculative_margin, &contact);
        }
        else if (c0->type == PHYS_SHAPE_BOX && c1->type == PHYS_SHAPE_BOX) {
            phys_vec3_t he0 = args->boxes[c0->shape_index].half_extents;
            phys_vec3_t he1 = args->boxes[c1->shape_index].half_extents;
            phys_contact_point_t contacts_buf[4];
            int nc = phys_box_vs_box(w0, q0, he0, w1, q1, he1,
                                     contacts_buf, 4,
                                     args->speculative_margin);
            if (nc > 0) {
                /* Claim a slot atomically. */
                uint32_t slot = atomic_fetch_add(&shared->out_idx, 1);
                if (slot < args->max_candidates) {
                    phys_contact_candidate_t *cand = &args->candidates_out[slot];
                    cand->body_a = ba;
                    cand->body_b = bb;
                    cand->contact_count = (uint8_t)(nc > 4 ? 4 : nc);
                    for (int j = 0; j < cand->contact_count; j++) {
                        cand->contacts[j] = contacts_buf[j];
                    }
                }
                continue; /* Already emitted. */
            }
        }
        else if (c0->type == PHYS_SHAPE_BOX && c1->type == PHYS_SHAPE_CAPSULE) {
            phys_vec3_t he = args->boxes[c0->shape_index].half_extents;
            float rc = args->capsules[c1->shape_index].radius;
            float hh = args->capsules[c1->shape_index].half_height;
            hit = phys_box_vs_capsule(w0, q0, he, w1, q1, rc, hh,
                                      args->speculative_margin, &contact);
        }
        else if (c0->type == PHYS_SHAPE_CAPSULE && c1->type == PHYS_SHAPE_CAPSULE) {
            float r0c = args->capsules[c0->shape_index].radius;
            float h0  = args->capsules[c0->shape_index].half_height;
            float r1c = args->capsules[c1->shape_index].radius;
            float h1  = args->capsules[c1->shape_index].half_height;
            hit = phys_capsule_vs_capsule(w0, q0, r0c, h0,
                                          w1, q1, r1c, h1,
                                          args->speculative_margin, &contact);
        }
        /* ── Convex hull dispatch ──────────────────────────────── */
        else if (c0->type == PHYS_SHAPE_SPHERE && c1->type == PHYS_SHAPE_CONVEX) {
            float rs = args->spheres[c0->shape_index].radius;
            const phys_convex_hull_t *hull = &args->convex_hulls[c1->shape_index];
            hit = phys_sphere_vs_convex(w0, rs, w1, q1, hull,
                                        args->speculative_margin, &contact);
        }
        else if (c0->type == PHYS_SHAPE_BOX && c1->type == PHYS_SHAPE_CONVEX) {
            phys_vec3_t he = args->boxes[c0->shape_index].half_extents;
            const phys_convex_hull_t *hull = &args->convex_hulls[c1->shape_index];
            hit = phys_box_vs_convex(w0, q0, he, w1, q1, hull,
                                     args->speculative_margin, &contact);
        }
        else if (c0->type == PHYS_SHAPE_CAPSULE && c1->type == PHYS_SHAPE_CONVEX) {
            float rc = args->capsules[c0->shape_index].radius;
            float hh = args->capsules[c0->shape_index].half_height;
            const phys_convex_hull_t *hull = &args->convex_hulls[c1->shape_index];
            hit = phys_capsule_vs_convex(w0, q0, rc, hh, w1, q1, hull,
                                          args->speculative_margin, &contact);
        }
        else if (c0->type == PHYS_SHAPE_CONVEX && c1->type == PHYS_SHAPE_CONVEX) {
            const phys_convex_hull_t *ha = &args->convex_hulls[c0->shape_index];
            const phys_convex_hull_t *hb = &args->convex_hulls[c1->shape_index];
            hit = phys_convex_vs_convex(w0, q0, ha, w1, q1, hb,
                                        args->speculative_margin, &contact);
        }
        /* ── Compound dispatch ─────────────────────────────────── */
        else if (c1->type == PHYS_SHAPE_COMPOUND) {
            const phys_convex_compound_t *cc =
                &args->compounds[c1->shape_index];
            for (uint32_t ci = 0; ci < cc->child_count; ci++) {
                const phys_convex_hull_t *hull =
                    &args->convex_hulls[cc->child_hull_indices[ci]];
                phys_contact_point_t child_contact;
                memset(&child_contact, 0, sizeof(child_contact));
                bool child_hit = false;

                if (c0->type == PHYS_SHAPE_SPHERE) {
                    float rs = args->spheres[c0->shape_index].radius;
                    child_hit = phys_sphere_vs_convex(w0, rs, w1, q1, hull,
                                                      args->speculative_margin,
                                                      &child_contact);
                } else if (c0->type == PHYS_SHAPE_BOX) {
                    phys_vec3_t he = args->boxes[c0->shape_index].half_extents;
                    child_hit = phys_box_vs_convex(w0, q0, he, w1, q1, hull,
                                                    args->speculative_margin,
                                                    &child_contact);
                } else if (c0->type == PHYS_SHAPE_CAPSULE) {
                    float rc = args->capsules[c0->shape_index].radius;
                    float hh = args->capsules[c0->shape_index].half_height;
                    child_hit = phys_capsule_vs_convex(w0, q0, rc, hh,
                                                       w1, q1, hull,
                                                       args->speculative_margin,
                                                       &child_contact);
                } else if (c0->type == PHYS_SHAPE_COMPOUND) {
                    const phys_convex_compound_t *cc0 =
                        &args->compounds[c0->shape_index];
                    for (uint32_t cj = 0; cj < cc0->child_count; cj++) {
                        const phys_convex_hull_t *hull0 =
                            &args->convex_hulls[cc0->child_hull_indices[cj]];
                        phys_contact_point_t cc_contact;
                        memset(&cc_contact, 0, sizeof(cc_contact));
                        if (phys_convex_vs_convex(w0, q0, hull0, w1, q1, hull,
                                                   args->speculative_margin,
                                                   &cc_contact)) {
                            uint32_t slot = atomic_fetch_add(&shared->out_idx, 1);
                            if (slot < args->max_candidates) {
                                phys_contact_candidate_t *cand = &args->candidates_out[slot];
                                cand->body_a = ba;
                                cand->body_b = bb;
                                cand->contacts[0] = cc_contact;
                                cand->contact_count = 1;
                            }
                        }
                    }
                    continue;
                }

                if (child_hit) {
                    uint32_t slot = atomic_fetch_add(&shared->out_idx, 1);
                    if (slot < args->max_candidates) {
                        phys_contact_candidate_t *cand = &args->candidates_out[slot];
                        cand->body_a = ba;
                        cand->body_b = bb;
                        cand->contacts[0] = child_contact;
                        cand->contact_count = 1;
                    }
                }
            }
            continue;
        }
        else if (c0->type == PHYS_SHAPE_SPHERE && c1->type == PHYS_SHAPE_MESH) {
            float rs = args->spheres[c0->shape_index].radius;
            const phys_mesh_shape_t *ms = &args->meshes[c1->shape_index];
            phys_vec3_t local_w0 = vec3_sub(w0, w1);
            phys_contact_point_t contacts_buf[4];
            int nc = phys_sphere_vs_mesh(local_w0, rs, ms->triangles, &ms->bvh,
                                          args->speculative_margin, ms->solid,
                                          contacts_buf, 4);
            if (nc > 0) {
                uint32_t slot = atomic_fetch_add(&shared->out_idx, 1);
                if (slot < args->max_candidates) {
                    phys_contact_candidate_t *cand = &args->candidates_out[slot];
                    cand->body_a = ba;
                    cand->body_b = bb;
                    cand->contact_count = (uint8_t)(nc > 4 ? 4 : nc);
                    for (int j = 0; j < cand->contact_count; j++) {
                        cand->contacts[j] = contacts_buf[j];
                        cand->contacts[j].point_world = vec3_add(
                            cand->contacts[j].point_world, w1);
                        cand->contacts[j].normal = vec3_scale(
                            cand->contacts[j].normal, -1.0f);
                    }
                }
                continue;
            }
        }
        else if (c0->type == PHYS_SHAPE_BOX && c1->type == PHYS_SHAPE_MESH) {
            phys_vec3_t he = args->boxes[c0->shape_index].half_extents;
            const phys_mesh_shape_t *ms = &args->meshes[c1->shape_index];
            phys_vec3_t local_w0 = vec3_sub(w0, w1);
            phys_contact_point_t contacts_buf[4];
            int nc = phys_box_vs_mesh(local_w0, q0, he, ms->triangles, &ms->bvh,
                                       args->speculative_margin,
                                       contacts_buf, 4);
            if (nc > 0) {
                uint32_t slot = atomic_fetch_add(&shared->out_idx, 1);
                if (slot < args->max_candidates) {
                    phys_contact_candidate_t *cand = &args->candidates_out[slot];
                    cand->body_a = ba;
                    cand->body_b = bb;
                    cand->contact_count = (uint8_t)(nc > 4 ? 4 : nc);
                    for (int j = 0; j < cand->contact_count; j++) {
                        cand->contacts[j] = contacts_buf[j];
                        cand->contacts[j].point_world = vec3_add(
                            cand->contacts[j].point_world, w1);
                        cand->contacts[j].normal = vec3_scale(
                            cand->contacts[j].normal, -1.0f);
                    }
                }
                continue;
            }
        }
        else if (c0->type == PHYS_SHAPE_CAPSULE && c1->type == PHYS_SHAPE_MESH) {
            const phys_capsule_t *cap = &args->capsules[c0->shape_index];
            const phys_mesh_shape_t *ms = &args->meshes[c1->shape_index];
            phys_vec3_t local_w0 = vec3_sub(w0, w1);
            phys_contact_point_t contacts_buf[4];
            int nc = phys_capsule_vs_mesh(local_w0, q0, cap->radius, cap->half_height,
                                           ms->triangles, &ms->bvh,
                                           args->speculative_margin, ms->solid,
                                           contacts_buf, 4);
            if (nc > 0) {
                uint32_t slot = atomic_fetch_add(&shared->out_idx, 1);
                if (slot < args->max_candidates) {
                    phys_contact_candidate_t *cand = &args->candidates_out[slot];
                    cand->body_a = ba;
                    cand->body_b = bb;
                    cand->contact_count = (uint8_t)(nc > 4 ? 4 : nc);
                    for (int j = 0; j < cand->contact_count; j++) {
                        cand->contacts[j] = contacts_buf[j];
                        cand->contacts[j].point_world = vec3_add(
                            cand->contacts[j].point_world, w1);
                        cand->contacts[j].normal = vec3_scale(
                            cand->contacts[j].normal, -1.0f);
                    }
                }
                continue;
            }
        }
        else if (c0->type == PHYS_SHAPE_SPHERE && c1->type == PHYS_SHAPE_HALFSPACE) {
            float rs = args->spheres[c0->shape_index].radius;
            const phys_halfspace_t *hs = &args->halfspaces[c1->shape_index];
            hit = phys_sphere_vs_halfspace(w0, rs,
                                            hs->normal, hs->distance,
                                            args->speculative_margin, &contact);
        }
        else if (c0->type == PHYS_SHAPE_BOX && c1->type == PHYS_SHAPE_HALFSPACE) {
            phys_vec3_t he = args->boxes[c0->shape_index].half_extents;
            const phys_halfspace_t *hs = &args->halfspaces[c1->shape_index];
            phys_contact_point_t contacts_buf[4];
            int nc = phys_box_vs_halfspace(w0, q0, he,
                                            hs->normal, hs->distance,
                                            args->speculative_margin,
                                            contacts_buf, 4);
            if (nc > 0) {
                uint32_t slot = atomic_fetch_add(&shared->out_idx, 1);
                if (slot < args->max_candidates) {
                    phys_contact_candidate_t *cand = &args->candidates_out[slot];
                    cand->body_a = ba;
                    cand->body_b = bb;
                    cand->contact_count = (uint8_t)(nc > 4 ? 4 : nc);
                    for (int j = 0; j < cand->contact_count; j++) {
                        cand->contacts[j] = contacts_buf[j];
                    }
                }
                continue;
            }
        }
        else if (c0->type == PHYS_SHAPE_CAPSULE && c1->type == PHYS_SHAPE_HALFSPACE) {
            float rc = args->capsules[c0->shape_index].radius;
            float hh = args->capsules[c0->shape_index].half_height;
            const phys_halfspace_t *hs = &args->halfspaces[c1->shape_index];
            hit = phys_capsule_vs_halfspace(w0, q0, rc, hh,
                                             hs->normal, hs->distance,
                                             args->speculative_margin, &contact);
        }
        /* mesh-halfspace and halfspace-halfspace: no collision. */

        if (hit) {
            /* Claim a slot atomically. */
            uint32_t slot = atomic_fetch_add(&shared->out_idx, 1);
            if (slot < args->max_candidates) {
                phys_contact_candidate_t *cand = &args->candidates_out[slot];
                cand->body_a = ba;
                cand->body_b = bb;
                cand->contacts[0] = contact;
                cand->contact_count = 1;
            }
        }
    }
}

/* ── Public API ────────────────────────────────────────────────── */

void phys_stage_narrowphase_par(const phys_narrowphase_args_t *args,
                                phys_job_context_t *ctx,
                                phys_frame_arena_t *arena)
{
    if (!args || !args->bodies || !args->colliders || !args->pairs
        || !args->candidates_out || !args->candidate_count_out) {
        return;
    }

    /* Fall back to sequential if no job context. */
    if (!ctx || !arena) {
        phys_stage_narrowphase(args);
        return;
    }

    /* Zero pairs → nothing to do. */
    if (args->pair_count == 0) {
        *args->candidate_count_out = 0;
        return;
    }

    /* Initialize shared state with atomic output index at 0. */
    np_par_shared_t shared;
    shared.args = args;
    atomic_init(&shared.out_idx, 0);

    /* Compute number of batches. */
    uint32_t num_batches = (args->pair_count + NP_PAR_BATCH_SIZE - 1) / NP_PAR_BATCH_SIZE;

    /* Allocate batch descriptors from the frame arena. */
    phys_job_batch_t *batches = phys_frame_arena_alloc(
        arena, num_batches * sizeof(phys_job_batch_t),
        _Alignof(phys_job_batch_t));
    if (!batches) {
        phys_stage_narrowphase(args);
        return;
    }

    phys_dispatch_stage(ctx, PHYS_STAGE_NARROWPHASE,
                        np_par_job_fn, &shared,
                        args->pair_count, NP_PAR_BATCH_SIZE,
                        batches);

    phys_wait_stage(ctx, PHYS_STAGE_NARROWPHASE);

    /* Clamp the output count to max_candidates (atomic may exceed it). */
    uint32_t total = atomic_load(&shared.out_idx);
    if (total > args->max_candidates) {
        total = args->max_candidates;
    }
    *args->candidate_count_out = total;
}
