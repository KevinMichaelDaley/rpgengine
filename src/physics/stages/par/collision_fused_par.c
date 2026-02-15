/**
 * @file collision_fused_par.c
 * @brief Fused parallel collision pipeline: narrow→manifold→stab→constraint.
 *
 * Each job processes a batch of broadphase pairs through all four stages
 * sequentially within a single fiber, eliminating 3 dispatch+barrier
 * cycles per substep.  Intermediate candidates, manifolds, and hints
 * live on the stack; only final manifolds and constraints are written
 * to shared output arrays via atomics.
 *
 * Non-static functions: 1 (phys_stage_collision_fused_par).
 */

#include "ferrum/physics/par/collision_fused_par.h"

#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

#include "ferrum/job/spinlock.h"
#include "ferrum/math/vec3.h"
#include "ferrum/physics/collision/box_box.h"
#include "ferrum/physics/collision/box_capsule.h"
#include "ferrum/physics/collision/capsule_capsule.h"
#include "ferrum/physics/mesh_narrowphase.h"
#include "ferrum/physics/step_plan.h"

#ifdef TRACY_ENABLE
#include "tracy/TracyC.h"
#endif

/* ── Constants ─────────────────────────────────────────────────── */

/** Max candidates per batch (1 per pair). */
#define FUSED_MAX_CAND_PER_BATCH PHYS_COLLISION_FUSED_BATCH_SIZE

/** Max manifolds per batch (1 per candidate). */
#define FUSED_MAX_MANI_PER_BATCH PHYS_COLLISION_FUSED_BATCH_SIZE

/** Max constraints per batch (4 contact points × 1 constraint each). */
#define FUSED_MAX_CONS_PER_BATCH (PHYS_COLLISION_FUSED_BATCH_SIZE * PHYS_MAX_MANIFOLD_POINTS)

/** Tolerance for unstable box edge/corner contacts. */
#define FACE_TOL 0.05f

/* ── Shared context ────────────────────────────────────────────── */

/**
 * @brief Shared state across all fused collision jobs.
 */
typedef struct collision_fused_shared {
    const phys_collision_fused_args_t *args; /**< Caller's fused args. */

    /* Manifold output. */
    atomic_uint manifold_idx;     /**< Next free manifold slot. */

    /* Constraint output. */
    atomic_uint constraint_idx;   /**< Next free constraint slot. */

    /* Cache protection (fiber-safe). */
    job_spinlock_t cache_mtx;
} collision_fused_shared_t;

/* ── Helpers ───────────────────────────────────────────────────── */

/**
 * @brief Check if a box contact is on an edge or corner (unstable).
 */
static int box_contact_unstable(phys_vec3_t local, phys_vec3_t half_ext)
{
    int boundary_count = 0;
    const float coords[3] = { local.x, local.y, local.z };
    const float extents[3] = { half_ext.x, half_ext.y, half_ext.z };
    for (int a = 0; a < 3; ++a) {
        if (extents[a] <= 0.0f) continue;
        float limit = extents[a] * (1.0f - FACE_TOL);
        if (fabsf(coords[a]) >= limit) boundary_count++;
    }
    return boundary_count >= 2;
}

/**
 * @brief Check if a manifold contact is on an unstable box support.
 */
static int contact_on_unstable_box(const phys_collision_fused_args_t *args,
                                   const phys_manifold_t *m,
                                   const phys_contact_point_t *cp)
{
    if (!args->colliders || !args->boxes) return 0;

    const phys_collider_t *ca = &args->colliders[m->body_a];
    if (ca->type == PHYS_SHAPE_BOX) {
        phys_vec3_t he = args->boxes[ca->shape_index].half_extents;
        if (box_contact_unstable(cp->local_a, he)) return 1;
    }
    const phys_collider_t *cb = &args->colliders[m->body_b];
    if (cb->type == PHYS_SHAPE_BOX) {
        phys_vec3_t he = args->boxes[cb->shape_index].half_extents;
        if (box_contact_unstable(cp->local_b, he)) return 1;
    }
    return 0;
}

/* ── Narrowphase: test one pair ────────────────────────────────── */

/**
 * @brief Run narrowphase on one broadphase pair; write candidate to local buf.
 * @return 1 if a candidate was written, 0 otherwise.
 */
static int narrow_test_pair(const phys_collision_fused_args_t *args,
                            uint32_t pair_idx,
                            phys_contact_candidate_t *cand_out)
{
    uint32_t a = args->pairs[pair_idx].body_a;
    uint32_t b = args->pairs[pair_idx].body_b;

    /* Skip pairs where both bodies are sleeping. */
    if (phys_body_is_sleeping(&args->bodies[a]) &&
        phys_body_is_sleeping(&args->bodies[b])) {
        return 0;
    }

    const phys_collider_t *ca = &args->colliders[a];
    const phys_collider_t *cb = &args->colliders[b];

    phys_vec3_t wa = phys_collider_world_center(ca, args->bodies[a].position,
                                                 args->bodies[a].orientation);
    phys_quat_t qa = phys_collider_world_rotation(ca, args->bodies[a].orientation);
    phys_vec3_t wb = phys_collider_world_center(cb, args->bodies[b].position,
                                                 args->bodies[b].orientation);
    phys_quat_t qb = phys_collider_world_rotation(cb, args->bodies[b].orientation);

    /* Normalize so type_a <= type_b. */
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
            cand_out->body_a = ba;
            cand_out->body_b = bb;
            cand_out->contact_count = (uint8_t)(nc > 4 ? 4 : nc);
            for (int j = 0; j < cand_out->contact_count; j++) {
                cand_out->contacts[j] = contacts_buf[j];
            }
            return 1;
        }
        return 0;
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
    else if (c0->type == PHYS_SHAPE_SPHERE && c1->type == PHYS_SHAPE_MESH) {
        float rs = args->spheres[c0->shape_index].radius;
        const phys_mesh_shape_t *ms = &args->meshes[c1->shape_index];
        /* Transform primitive into mesh-local space. */
        phys_vec3_t local_w0 = vec3_sub(w0, w1);
        phys_contact_point_t contacts_buf[4];
        int nc = phys_sphere_vs_mesh(local_w0, rs, ms->triangles, &ms->bvh,
                                      args->speculative_margin,
                                      contacts_buf, 4);
        if (nc > 0) {
            cand_out->body_a = ba;
            cand_out->body_b = bb;
            cand_out->contact_count = (uint8_t)(nc > 4 ? 4 : nc);
            for (int j = 0; j < cand_out->contact_count; j++) {
                cand_out->contacts[j] = contacts_buf[j];
                cand_out->contacts[j].point_world = vec3_add(
                    cand_out->contacts[j].point_world, w1);
                cand_out->contacts[j].normal = vec3_scale(
                    cand_out->contacts[j].normal, -1.0f);
            }
            return 1;
        }
        return 0;
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
            cand_out->body_a = ba;
            cand_out->body_b = bb;
            cand_out->contact_count = (uint8_t)(nc > 4 ? 4 : nc);
            for (int j = 0; j < cand_out->contact_count; j++) {
                cand_out->contacts[j] = contacts_buf[j];
                cand_out->contacts[j].point_world = vec3_add(
                    cand_out->contacts[j].point_world, w1);
                cand_out->contacts[j].normal = vec3_scale(
                    cand_out->contacts[j].normal, -1.0f);
            }
            return 1;
        }
        return 0;
    }
    else if (c0->type == PHYS_SHAPE_CAPSULE && c1->type == PHYS_SHAPE_MESH) {
        const phys_capsule_t *cap = &args->capsules[c0->shape_index];
        const phys_mesh_shape_t *ms = &args->meshes[c1->shape_index];
        phys_vec3_t local_w0 = vec3_sub(w0, w1);
        phys_contact_point_t contacts_buf[4];
        int nc = phys_capsule_vs_mesh(local_w0, q0, cap->radius, cap->half_height,
                                       ms->triangles, &ms->bvh,
                                       args->speculative_margin,
                                       contacts_buf, 4);
        if (nc > 0) {
            cand_out->body_a = ba;
            cand_out->body_b = bb;
            cand_out->contact_count = (uint8_t)(nc > 4 ? 4 : nc);
            for (int j = 0; j < cand_out->contact_count; j++) {
                cand_out->contacts[j] = contacts_buf[j];
                cand_out->contacts[j].point_world = vec3_add(
                    cand_out->contacts[j].point_world, w1);
                cand_out->contacts[j].normal = vec3_scale(
                    cand_out->contacts[j].normal, -1.0f);
            }
            return 1;
        }
        return 0;
    }

    if (hit) {
        cand_out->body_a = ba;
        cand_out->body_b = bb;
        cand_out->contacts[0] = contact;
        cand_out->contact_count = 1;
        return 1;
    }
    return 0;
}

/* ── Manifold build: process one candidate ─────────────────────── */

/**
 * @brief Build manifold from candidate using shared cache.
 * @return 1 if manifold was written, 0 otherwise.
 */
static int manifold_build_one(collision_fused_shared_t *shared,
                              const phys_contact_candidate_t *cand,
                              phys_manifold_t *mani_out)
{
    const phys_collision_fused_args_t *args = shared->args;

    job_spinlock_lock(&shared->cache_mtx);

    phys_manifold_t *cached = phys_manifold_cache_get_or_create(
        args->cache, cand->body_a, cand->body_b, (uint32_t)args->tick);
    if (!cached) {
        job_spinlock_unlock(&shared->cache_mtx);
        return 0;
    }

    /* Save old impulses for warmstart matching. */
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

    /* Clear and rebuild. */
    phys_manifold_clear(cached);
    for (uint8_t j = 0; j < PHYS_MAX_MANIFOLD_POINTS; ++j) {
        cached->normal_impulse[j]     = 0.0f;
        cached->tangent_impulse[j][0] = 0.0f;
        cached->tangent_impulse[j][1] = 0.0f;
    }
    cached->body_a = cand->body_a;
    cached->body_b = cand->body_b;

    if (args->bodies) {
        cached->friction = phys_combine_friction(
            args->bodies[cand->body_a].friction,
            args->bodies[cand->body_b].friction);
        cached->restitution = phys_combine_restitution(
            args->bodies[cand->body_a].restitution,
            args->bodies[cand->body_b].restitution);
    }

    for (uint8_t j = 0; j < cand->contact_count; ++j) {
        phys_manifold_add_point(cached, &cand->contacts[j]);
    }

    /* Match warmstart impulses by feature ID. */
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

    /* Copy before unlocking. */
    *mani_out = *cached;

    job_spinlock_unlock(&shared->cache_mtx);
    return 1;
}

/* ── Stabilization: compute one hint ───────────────────────────── */

/**
 * @brief Compute stabilization hint for one manifold.
 */
static void stab_compute_one(const phys_collision_fused_args_t *args,
                             const phys_manifold_t *m,
                             phys_stab_hint_t *hint)
{
    const phys_body_t *body_a = &args->bodies[m->body_a];
    const phys_body_t *body_b = &args->bodies[m->body_b];

    uint8_t effective_tier = body_a->tier > body_b->tier
                                 ? body_a->tier : body_b->tier;

    float tier_friction_boost;
    float tier_velocity_damping;
    phys_tier_stabilization_params((phys_tier_t)effective_tier,
                                   &tier_friction_boost,
                                   &tier_velocity_damping);

    hint->friction_boost   = tier_friction_boost;
    hint->velocity_damping = tier_velocity_damping;
    hint->friction_scale    = 1.0f;
    hint->restitution_scale = 1.0f;

    if (m->point_count == 0) return;

    const phys_contact_point_t *cp = &m->points[0];
    const float threshold = args->resting_velocity_threshold;
    const float threshold_sq = threshold * threshold;

    vec3_t r_a = vec3_sub(cp->point_world, body_a->position);
    vec3_t r_b = vec3_sub(cp->point_world, body_b->position);
    vec3_t v_a = vec3_add(body_a->linear_vel,
                          vec3_cross(body_a->angular_vel, r_a));
    vec3_t v_b = vec3_add(body_b->linear_vel,
                          vec3_cross(body_b->angular_vel, r_b));
    vec3_t v_rel = vec3_sub(v_a, v_b);

    float v_n = vec3_dot(v_rel, cp->normal);
    float v_rel_sq = vec3_dot(v_rel, v_rel);
    float v_t_sq = v_rel_sq - v_n * v_n;
    if (v_t_sq < 0.0f) v_t_sq = 0.0f;

    if (fabsf(v_n) < threshold && v_t_sq < threshold_sq) {
        if (!contact_on_unstable_box(args, m, cp)) {
            hint->friction_scale    = 3.0f * tier_friction_boost;
            hint->restitution_scale = 0.0f;
        }
    }
}

/* ── Constraint build: build from one manifold ─────────────────── */

/**
 * @brief Build constraints for one manifold + hint into local buffer.
 * @return Number of constraints written.
 */
static uint32_t constraint_build_one(const phys_collision_fused_args_t *args,
                                     const phys_manifold_t *manifold,
                                     const phys_stab_hint_t *hint,
                                     uint32_t manifold_output_idx,
                                     phys_constraint_t *cons_out,
                                     uint32_t max_cons)
{
    const phys_body_t *body_a = &args->bodies[manifold->body_a];
    const phys_body_t *body_b = &args->bodies[manifold->body_b];

    float friction    = manifold->friction    * hint->friction_scale;
    float restitution = manifold->restitution * hint->restitution_scale;

    uint32_t written = 0;
    for (uint8_t p = 0; p < manifold->point_count && written < max_cons; ++p) {
        phys_constraint_t *c = &cons_out[written];
        written++;

        phys_constraint_build_contact(c, body_a, body_b,
                                       &manifold->points[p],
                                       friction, restitution,
                                       args->dt, args->baumgarte,
                                       args->slop);

        c->body_a       = manifold->body_a;
        c->body_b       = manifold->body_b;
        c->manifold_idx = manifold_output_idx;
        c->point_idx    = (uint8_t)p;

        c->solver_mode = (uint8_t)phys_tier_cross_solver_mode(
            (phys_tier_t)body_a->tier, (phys_tier_t)body_b->tier);

        /* Warmstart impulses from manifold cache. */
        c->rows[0].lambda = manifold->normal_impulse[p];
        c->rows[1].lambda = manifold->tangent_impulse[p][0];
        c->rows[2].lambda = manifold->tangent_impulse[p][1];
        for (uint8_t r = 0; r < 3; ++r) {
            if (isnan(c->rows[r].lambda) || isinf(c->rows[r].lambda)) {
                c->rows[r].lambda = 0.0f;
            }
        }
    }
    return written;
}

/* ── Fused job function ────────────────────────────────────────── */

/**
 * @brief Process a batch of broadphase pairs through the full collision
 *        pipeline: narrowphase → manifold build → stabilization →
 *        constraint build.
 *
 * Each step operates on local stack buffers.  Only manifolds and
 * constraints are written to the shared output arrays via atomics.
 */
static void collision_fused_job(void *data)
{
    phys_job_batch_t *batch = data;
    collision_fused_shared_t *shared = batch->user_args;
    const phys_collision_fused_args_t *args = shared->args;

    uint32_t pair_start = batch->start;
    uint32_t pair_end   = pair_start + batch->count;
    if (pair_end > args->pair_count) pair_end = args->pair_count;

    /* ── Phase 1: Narrowphase — pairs → local candidates ──────── */
    phys_contact_candidate_t local_cands[FUSED_MAX_CAND_PER_BATCH];
    uint32_t cand_count = 0;

    for (uint32_t i = pair_start; i < pair_end; ++i) {
        if (cand_count >= FUSED_MAX_CAND_PER_BATCH) break;
        if (narrow_test_pair(args, i, &local_cands[cand_count])) {
            cand_count++;
        }
    }

    if (cand_count == 0) return;

    /* ── Phase 2: Manifold build — candidates → local manifolds ── */
    phys_manifold_t local_manis[FUSED_MAX_MANI_PER_BATCH];
    uint32_t mani_count = 0;

    for (uint32_t i = 0; i < cand_count; ++i) {
        if (mani_count >= FUSED_MAX_MANI_PER_BATCH) break;
        if (manifold_build_one(shared, &local_cands[i],
                               &local_manis[mani_count])) {
            mani_count++;
        }
    }

    if (mani_count == 0) return;

    /* ── Phase 3+4: Stab + Constraint build — manifolds → constraints ── */

    /* First pass: count total constraints for this batch. */
    uint32_t total_cons = 0;
    for (uint32_t i = 0; i < mani_count; ++i) {
        total_cons += local_manis[i].point_count;
    }

    /* Atomically claim output slots for manifolds and constraints. */
    uint32_t mani_base = atomic_fetch_add(&shared->manifold_idx, mani_count);
    uint32_t cons_base = (total_cons > 0)
                         ? atomic_fetch_add(&shared->constraint_idx, total_cons)
                         : 0;

    /* Copy manifolds to shared output. */
    uint32_t mani_avail = 0;
    if (mani_base < args->max_manifolds) {
        mani_avail = args->max_manifolds - mani_base;
        if (mani_avail > mani_count) mani_avail = mani_count;
        memcpy(&args->manifolds_out[mani_base], local_manis,
               mani_avail * sizeof(phys_manifold_t));
    }

    /* Build constraints directly into shared output. */
    if (total_cons == 0 || cons_base >= args->max_constraints) return;

    uint32_t cons_avail = args->max_constraints - cons_base;
    uint32_t cons_written = 0;

    for (uint32_t i = 0; i < mani_avail && cons_written < cons_avail; ++i) {
        phys_stab_hint_t hint;
        stab_compute_one(args, &local_manis[i], &hint);

        /* manifold_output_idx is the global index in manifolds_out. */
        uint32_t mani_global_idx = mani_base + i;

        uint32_t n = constraint_build_one(
            args, &local_manis[i], &hint, mani_global_idx,
            &args->constraints_out[cons_base + cons_written],
            cons_avail - cons_written);
        cons_written += n;
    }
}

/* ── Public API ────────────────────────────────────────────────── */

void phys_stage_collision_fused_par(const phys_collision_fused_args_t *args,
                                     phys_job_context_t *ctx,
                                     phys_frame_arena_t *arena)
{
    if (!args || !ctx || !arena) return;
    if (!args->bodies || !args->colliders || !args->pairs) return;
    if (!args->manifolds_out || !args->manifold_count_out) return;
    if (!args->constraints_out || !args->constraint_count_out) return;

    if (args->pair_count == 0) {
        *args->manifold_count_out = 0;
        *args->constraint_count_out = 0;
        return;
    }

    /* Set up shared context. */
    collision_fused_shared_t shared = {
        .args = args,
    };
    atomic_init(&shared.manifold_idx, 0);
    atomic_init(&shared.constraint_idx, 0);
    job_spinlock_init(&shared.cache_mtx);

    /* Allocate batch descriptors. */
    uint32_t batch_size = phys_batch_size(ctx, args->pair_count,
                                          64, PHYS_COLLISION_FUSED_BATCH_SIZE);
    uint32_t num_batches = (args->pair_count + batch_size - 1) / batch_size;

    phys_job_batch_t *batches = phys_frame_arena_alloc(
        arena, num_batches * sizeof(phys_job_batch_t),
        _Alignof(phys_job_batch_t));
    if (!batches) {
        /* Can't allocate — fall back to zero output.  Caller should
         * have ensured arena capacity. */
        *args->manifold_count_out = 0;
        *args->constraint_count_out = 0;
        job_spinlock_destroy(&shared.cache_mtx);
        return;
    }

#ifdef TRACY_ENABLE
    TracyCZoneN(z_fused, "Phys.Collision.Fused", true);
#endif

    phys_dispatch_stage(ctx, PHYS_STAGE_COLLISION_FUSED,
                        collision_fused_job, &shared,
                        args->pair_count, batch_size, batches);

    phys_wait_stage(ctx, PHYS_STAGE_COLLISION_FUSED);

#ifdef TRACY_ENABLE
    TracyCZoneEnd(z_fused);
#endif

    /* Clamp output counts. */
    uint32_t final_mani = atomic_load(&shared.manifold_idx);
    if (final_mani > args->max_manifolds) final_mani = args->max_manifolds;
    *args->manifold_count_out = final_mani;

    uint32_t final_cons = atomic_load(&shared.constraint_idx);
    if (final_cons > args->max_constraints) final_cons = args->max_constraints;
    *args->constraint_count_out = final_cons;

    job_spinlock_destroy(&shared.cache_mtx);
}
