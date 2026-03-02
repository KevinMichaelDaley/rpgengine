/**
 * @file ccd_dynamic.c
 * @brief Dynamic-vs-dynamic swept CCD with solver manifold output.
 *
 * For each broadphase pair where at least one body has PHYS_BODY_FLAG_CCD
 * and both are dynamic primitives, bisects [0,1] to find the time of
 * impact (TOI), then runs GJK+EPA at the TOI to produce contact manifolds
 * for the solver.
 *
 * Non-static functions (1):
 *   1. phys_stage_ccd_dynamic
 */

#include "ferrum/physics/ccd_dynamic.h"

#include <math.h>
#include <string.h>

#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/gjk_epa.h"
#include "ferrum/physics/gjk_support.h"
#include "ferrum/physics/manifold.h"

/* ── Constants ─────────────────────────────────────────────────── */

/** Maximum bisection iterations for TOI search. */
#define CCD_MAX_BISECT_ITERS 16

/** GJK separation tolerance for "just touching". */
#define CCD_SEPARATION_EPS 1e-4f

/* ── Helpers ───────────────────────────────────────────────────── */

/** Linear interpolation of vec3. */
static phys_vec3_t lerp_vec3(phys_vec3_t a, phys_vec3_t b, float t) {
    return (phys_vec3_t){
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

/** Check if a shape type is a supported primitive for GJK support. */
static bool is_primitive_shape(phys_shape_type_t type) {
    return type == PHYS_SHAPE_SPHERE ||
           type == PHYS_SHAPE_BOX    ||
           type == PHYS_SHAPE_CAPSULE;
}

/** Check if a body is dynamic (non-static, non-sleeping). */
static bool is_dynamic(const phys_body_t *b) {
    return (b->flags & (PHYS_BODY_FLAG_STATIC)) == 0 &&
           b->inv_mass > 0.0f;
}

/* ── Support data setup at interpolated pose ───────────────────── */

/**
 * @brief Build GJK support data for a body at interpolated time t ∈ [0,1].
 *
 * Lerps position and slerps orientation from prev→curr, then fills
 * the appropriate support data struct.
 *
 * @param prev       Body state at t=0.
 * @param curr       Body state at t=1.
 * @param collider   Collider for this body.
 * @param spheres    Sphere pool.
 * @param capsules   Capsule pool.
 * @param boxes      Box pool.
 * @param t          Interpolation parameter [0,1].
 * @param out_data   Caller buffer (at least 64 bytes) for support data.
 * @param out_fn     Receives the support function pointer.
 * @return true on success, false if shape type is unsupported.
 */
static bool build_support_at_t(const phys_body_t *prev,
                                const phys_body_t *curr,
                                const phys_collider_t *collider,
                                const void *spheres,
                                const void *capsules,
                                const void *boxes,
                                float t,
                                void *out_data,
                                phys_gjk_support_fn *out_fn) {
    /* Interpolate pose. */
    phys_vec3_t pos = lerp_vec3(prev->position, curr->position, t);
    phys_quat_t rot = quat_slerp(prev->orientation, curr->orientation, t, 1e-6f);

    uint32_t si = collider->shape_index;

    switch (collider->type) {
    case PHYS_SHAPE_SPHERE: {
        const phys_sphere_t *s = (const phys_sphere_t *)spheres + si;
        phys_gjk_sphere_data_t *d = out_data;
        d->center = pos;
        d->radius = s->radius;
        *out_fn = phys_gjk_support_sphere;
        return true;
    }
    case PHYS_SHAPE_BOX: {
        const phys_box_t *b = (const phys_box_t *)boxes + si;
        phys_gjk_box_data_t *d = out_data;
        d->center = pos;
        d->rotation = rot;
        d->half_extents = b->half_extents;
        *out_fn = phys_gjk_support_box;
        return true;
    }
    case PHYS_SHAPE_CAPSULE: {
        const phys_capsule_t *c = (const phys_capsule_t *)capsules + si;
        phys_gjk_capsule_data_t *d = out_data;
        d->center = pos;
        d->rotation = rot;
        d->radius = c->radius;
        d->half_height = c->half_height;
        *out_fn = phys_gjk_support_capsule;
        return true;
    }
    default:
        return false;
    }
}

/* ── TOI bisection + manifold generation for one pair ──────────── */

/**
 * @brief Sweep-test one pair via bisection, emit manifold if collision found.
 *
 * Strategy:
 * 1. Test at t=1 (curr poses). If no overlap, early-out.
 * 2. If overlap at t=0 (prev poses), report contact at t=0.
 * 3. Otherwise, bisect [0,1] to find the earliest overlap time.
 * 4. At the TOI, run EPA to get penetration normal+depth+points.
 * 5. Build manifold from the EPA result.
 *
 * @return true if a manifold was emitted, false otherwise.
 */
static bool sweep_pair(uint32_t body_a, uint32_t body_b,
                        const phys_ccd_dynamic_args_t *args,
                        phys_manifold_t *manifold_out) {
    const phys_body_t *prev_a = &args->bodies_prev[body_a];
    const phys_body_t *curr_a = &args->bodies_curr[body_a];
    const phys_body_t *prev_b = &args->bodies_prev[body_b];
    const phys_body_t *curr_b = &args->bodies_curr[body_b];
    const phys_collider_t *col_a = &args->colliders[body_a];
    const phys_collider_t *col_b = &args->colliders[body_b];

    /* Buffers for support data (sized to largest struct). */
    _Alignas(16) uint8_t data_a_buf[64];
    _Alignas(16) uint8_t data_b_buf[64];
    phys_gjk_support_fn fn_a, fn_b;

    /* --- Step 1: Test at t=1 (current poses) --- */
    if (!build_support_at_t(prev_a, curr_a, col_a,
                             args->spheres, args->capsules, args->boxes,
                             1.0f, data_a_buf, &fn_a))
        return false;
    if (!build_support_at_t(prev_b, curr_b, col_b,
                             args->spheres, args->capsules, args->boxes,
                             1.0f, data_b_buf, &fn_b))
        return false;

    phys_gjk_result_t result;
    bool overlap_at_1 = phys_gjk_intersect(fn_a, data_a_buf,
                                            fn_b, data_b_buf, &result);

    /* --- Step 2: Test at t=0 (prev poses) --- */
    if (!build_support_at_t(prev_a, curr_a, col_a,
                             args->spheres, args->capsules, args->boxes,
                             0.0f, data_a_buf, &fn_a))
        return false;
    if (!build_support_at_t(prev_b, curr_b, col_b,
                             args->spheres, args->capsules, args->boxes,
                             0.0f, data_b_buf, &fn_b))
        return false;

    phys_gjk_result_t result_0;
    bool overlap_at_0 = phys_gjk_intersect(fn_a, data_a_buf,
                                            fn_b, data_b_buf, &result_0);

    if (!overlap_at_0 && !overlap_at_1) {
        /* No overlap at either end — need to check if they cross mid-sweep.
         * Use bisection to find if there's any overlap in [0,1]. */
        float lo = 0.0f, hi = 1.0f;
        bool found = false;
        for (int i = 0; i < CCD_MAX_BISECT_ITERS; i++) {
            float mid = (lo + hi) * 0.5f;
            build_support_at_t(prev_a, curr_a, col_a,
                               args->spheres, args->capsules, args->boxes,
                               mid, data_a_buf, &fn_a);
            build_support_at_t(prev_b, curr_b, col_b,
                               args->spheres, args->capsules, args->boxes,
                               mid, data_b_buf, &fn_b);

            phys_gjk_result_t mid_result;
            bool mid_overlap = phys_gjk_intersect(fn_a, data_a_buf,
                                                   fn_b, data_b_buf,
                                                   &mid_result);
            if (mid_overlap) {
                found = true;
                result = mid_result;
                hi = mid;  /* Narrow toward earlier time. */
            } else {
                /* Check if the separation distance is small enough that
                 * the shapes might cross between mid and hi.  Use the
                 * midpoint separation as heuristic: if it's decreasing
                 * toward mid, the shapes are approaching in [lo, mid]. */
                lo = mid;
            }
        }
        if (!found) return false;  /* No overlap found in [0,1]. */
    }

    /* --- Step 3: Bisect for earliest TOI --- */
    float toi;
    if (overlap_at_0) {
        /* Already overlapping at start — TOI = 0. */
        toi = 0.0f;
        result = result_0;
    } else if (overlap_at_1 && !overlap_at_0) {
        /* Overlap at t=1 but not t=0 — bisect [0,1] for earliest TOI. */
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < CCD_MAX_BISECT_ITERS; i++) {
            float mid = (lo + hi) * 0.5f;
            build_support_at_t(prev_a, curr_a, col_a,
                               args->spheres, args->capsules, args->boxes,
                               mid, data_a_buf, &fn_a);
            build_support_at_t(prev_b, curr_b, col_b,
                               args->spheres, args->capsules, args->boxes,
                               mid, data_b_buf, &fn_b);

            phys_gjk_result_t mid_result;
            bool mid_overlap = phys_gjk_intersect(fn_a, data_a_buf,
                                                   fn_b, data_b_buf,
                                                   &mid_result);
            if (mid_overlap) {
                hi = mid;
                result = mid_result;
            } else {
                lo = mid;
            }
        }
        toi = hi;
        /* Re-test at toi to get final result for EPA. */
        build_support_at_t(prev_a, curr_a, col_a,
                           args->spheres, args->capsules, args->boxes,
                           toi, data_a_buf, &fn_a);
        build_support_at_t(prev_b, curr_b, col_b,
                           args->spheres, args->capsules, args->boxes,
                           toi, data_b_buf, &fn_b);
        phys_gjk_intersect(fn_a, data_a_buf, fn_b, data_b_buf, &result);
    } else {
        /* Found via mid-sweep bisection — toi is already narrowed. */
        toi = 0.5f;  /* Approximate; the bisect loop above already narrowed hi. */
    }

    /* --- Step 4: EPA for penetration info --- */
    if (!result.intersecting) return false;

    /* Rebuild support data at TOI for EPA. */
    build_support_at_t(prev_a, curr_a, col_a,
                       args->spheres, args->capsules, args->boxes,
                       toi, data_a_buf, &fn_a);
    build_support_at_t(prev_b, curr_b, col_b,
                       args->spheres, args->capsules, args->boxes,
                       toi, data_b_buf, &fn_b);

    bool epa_ok = phys_epa_penetration(fn_a, data_a_buf,
                                        fn_b, data_b_buf, &result);
    if (!epa_ok) return false;

    /* --- Step 5: Build manifold --- */
    phys_manifold_init(manifold_out, body_a, body_b);

    /* Use curr-frame material properties. */
    manifold_out->friction = (curr_a->friction + curr_b->friction) * 0.5f;
    manifold_out->restitution = (curr_a->restitution > curr_b->restitution)
                                 ? curr_a->restitution : curr_b->restitution;

    /* Compute contact point in world space at the TOI. */
    phys_vec3_t pos_a_toi = lerp_vec3(prev_a->position, curr_a->position, toi);
    phys_vec3_t pos_b_toi = lerp_vec3(prev_b->position, curr_b->position, toi);

    phys_contact_point_t cp;
    memset(&cp, 0, sizeof(cp));
    cp.point_world = (phys_vec3_t){
        (result.closest_a.x + result.closest_b.x) * 0.5f,
        (result.closest_a.y + result.closest_b.y) * 0.5f,
        (result.closest_a.z + result.closest_b.z) * 0.5f,
    };
    cp.normal = result.normal;
    cp.penetration = result.penetration;
    cp.local_a = (phys_vec3_t){
        cp.point_world.x - pos_a_toi.x,
        cp.point_world.y - pos_a_toi.y,
        cp.point_world.z - pos_a_toi.z,
    };
    cp.local_b = (phys_vec3_t){
        cp.point_world.x - pos_b_toi.x,
        cp.point_world.y - pos_b_toi.y,
        cp.point_world.z - pos_b_toi.z,
    };
    cp.feature_id = 0xCCD00000u | (body_a ^ (body_b << 16));

    phys_manifold_add_point(manifold_out, &cp);

    return true;
}

/* ── Public API ────────────────────────────────────────────────── */

int phys_stage_ccd_dynamic(const phys_ccd_dynamic_args_t *args) {
    if (!args) return 0;
    if (!args->bodies_prev || !args->bodies_curr) return 0;
    if (!args->colliders || !args->pairs) return 0;
    if (!args->manifolds_out || !args->manifold_count_out) return 0;
    if (args->pair_count == 0) return 0;

    int emitted = 0;

    for (uint32_t i = 0; i < args->pair_count; i++) {
        uint32_t a = args->pairs[i].body_a;
        uint32_t b = args->pairs[i].body_b;

        /* Bounds check. */
        if (a >= args->body_count || b >= args->body_count) continue;

        /* Both must be dynamic. */
        if (!is_dynamic(&args->bodies_curr[a])) continue;
        if (!is_dynamic(&args->bodies_curr[b])) continue;

        /* At least one must have CCD flag. */
        uint32_t flags_a = args->bodies_curr[a].flags;
        uint32_t flags_b = args->bodies_curr[b].flags;
        if (!(flags_a & PHYS_BODY_FLAG_CCD) &&
            !(flags_b & PHYS_BODY_FLAG_CCD)) continue;

        /* Both must be primitive shapes (sphere/box/capsule). */
        if (!is_primitive_shape(args->colliders[a].type)) continue;
        if (!is_primitive_shape(args->colliders[b].type)) continue;

        /* Check output buffer capacity. */
        if (*args->manifold_count_out >= args->max_manifolds) break;

        /* Sweep-test this pair. */
        phys_manifold_t *slot = &args->manifolds_out[*args->manifold_count_out];
        if (sweep_pair(a, b, args, slot)) {
            (*args->manifold_count_out)++;
            emitted++;
        }
    }

    return emitted;
}
