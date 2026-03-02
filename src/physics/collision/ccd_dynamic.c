/**
 * @file ccd_dynamic.c
 * @brief Dynamic-vs-dynamic swept CCD with solver manifold output.
 *
 * For each broadphase pair where at least one body has PHYS_BODY_FLAG_CCD
 * and both are dynamic primitives, extrapolates forward from current pose
 * using velocity × dt, bisects [0,1] to find the time of impact (TOI),
 * then runs GJK+EPA at the TOI to produce contact manifolds for the solver.
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
#include "ferrum/physics/joint.h"
#include "ferrum/physics/manifold.h"

/* ── Constants ─────────────────────────────────────────────────── */

/** Maximum bisection iterations for TOI search. */
#define CCD_MAX_BISECT_ITERS 16

/** GJK separation tolerance for "just touching". */
#define CCD_SEPARATION_EPS 1e-4f

/** Minimum relative speed (m/s) for a pair to be worth CCD testing.
 *  Below this the bodies can't tunnel through each other in one substep. */
#define CCD_MIN_RELATIVE_SPEED 0.5f

/* ── Helpers ───────────────────────────────────────────────────── */

/**
 * @brief Compute conservative bounding radius for a collider shape.
 *
 * Returns the maximum distance from the body center to any point on
 * the collider surface (circumradius).
 */
static float bounding_radius(const phys_collider_t *col,
                              const void *spheres,
                              const void *capsules,
                              const void *boxes) {
    uint32_t si = col->shape_index;
    switch (col->type) {
    case PHYS_SHAPE_SPHERE: {
        const phys_sphere_t *s = (const phys_sphere_t *)spheres + si;
        return s->radius;
    }
    case PHYS_SHAPE_BOX: {
        const phys_box_t *b = (const phys_box_t *)boxes + si;
        phys_vec3_t h = b->half_extents;
        return sqrtf(h.x * h.x + h.y * h.y + h.z * h.z);
    }
    case PHYS_SHAPE_CAPSULE: {
        const phys_capsule_t *c = (const phys_capsule_t *)capsules + si;
        return c->half_height + c->radius;
    }
    default:
        return 0.0f;
    }
}

/**
 * @brief Swept bounding-sphere overlap test.
 *
 * Each body sweeps a line segment from pos to pos + vel * dt.  We find
 * the minimum distance between the two segments and compare against the
 * sum of bounding radii.  This is a conservative filter — if this
 * returns false, the bodies cannot possibly collide during the substep.
 */
static bool swept_spheres_overlap(const phys_body_t *ba,
                                   const phys_body_t *bb,
                                   float r_a, float r_b, float dt) {
    /* Relative displacement: treat A as stationary. */
    float dx = bb->position.x - ba->position.x;
    float dy = bb->position.y - ba->position.y;
    float dz = bb->position.z - ba->position.z;

    float vx = (bb->linear_vel.x - ba->linear_vel.x) * dt;
    float vy = (bb->linear_vel.y - ba->linear_vel.y) * dt;
    float vz = (bb->linear_vel.z - ba->linear_vel.z) * dt;

    /* Closest point on segment [d, d+v] to origin → parameter t. */
    float v_dot_v = vx * vx + vy * vy + vz * vz;
    float t_min = 0.0f;
    if (v_dot_v > 1e-12f) {
        t_min = -(dx * vx + dy * vy + dz * vz) / v_dot_v;
        if (t_min < 0.0f) t_min = 0.0f;
        if (t_min > 1.0f) t_min = 1.0f;
    }

    /* Distance at closest approach. */
    float cx = dx + vx * t_min;
    float cy = dy + vy * t_min;
    float cz = dz + vz * t_min;
    float dist_sq = cx * cx + cy * cy + cz * cz;

    float r_sum = r_a + r_b;
    return dist_sq <= r_sum * r_sum;
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

/**
 * @brief Check if two bodies are directly connected by a joint.
 *
 * Linear scan over the joint array.  Joint counts are small enough
 * (typically < 1000) that this is cheaper than building a lookup table
 * per frame.
 */
static bool bodies_connected_by_joint(uint32_t a, uint32_t b,
                                       const phys_joint_t *joints,
                                       uint32_t joint_count) {
    for (uint32_t j = 0; j < joint_count; j++) {
        uint32_t ja = joints[j].body_a;
        uint32_t jb = joints[j].body_b;
        if ((ja == a && jb == b) || (ja == b && jb == a)) return true;
    }
    return false;
}

/**
 * @brief Extrapolate position forward by velocity × dt × t.
 */
static phys_vec3_t extrapolate_position(phys_vec3_t pos, phys_vec3_t vel,
                                         float dt, float t) {
    float s = dt * t;
    return (phys_vec3_t){
        pos.x + vel.x * s,
        pos.y + vel.y * s,
        pos.z + vel.z * s,
    };
}

/**
 * @brief Extrapolate orientation forward by angular velocity × dt × t.
 *
 * Uses the quaternion derivative: q' = q + 0.5 * dt*t * (0, omega) * q,
 * then normalizes.
 */
static phys_quat_t extrapolate_orientation(phys_quat_t q, phys_vec3_t omega,
                                            float dt, float t) {
    float s = dt * t * 0.5f;
    phys_quat_t dq = {
        .x = s * (omega.x * q.w + omega.y * q.z - omega.z * q.y),
        .y = s * (omega.y * q.w + omega.z * q.x - omega.x * q.z),
        .z = s * (omega.z * q.w + omega.x * q.y - omega.y * q.x),
        .w = s * (-omega.x * q.x - omega.y * q.y - omega.z * q.z),
    };
    phys_quat_t result = {
        .x = q.x + dq.x,
        .y = q.y + dq.y,
        .z = q.z + dq.z,
        .w = q.w + dq.w,
    };
    /* Normalize. */
    float len = sqrtf(result.x * result.x + result.y * result.y +
                      result.z * result.z + result.w * result.w);
    if (len > 1e-8f) {
        float inv = 1.0f / len;
        result.x *= inv;
        result.y *= inv;
        result.z *= inv;
        result.w *= inv;
    }
    return result;
}

/* ── Support data setup at extrapolated pose ───────────────────── */

/**
 * @brief Build GJK support data for a body at extrapolated time t ∈ [0,1].
 *
 * At t=0, uses the current pose.  At t=1, uses pose + velocity × dt.
 * Position is linearly extrapolated, orientation uses quaternion derivative.
 *
 * @param body      Current body state (position, orientation, velocities).
 * @param collider  Collider for this body.
 * @param spheres   Sphere pool.
 * @param capsules  Capsule pool.
 * @param boxes     Box pool.
 * @param dt        Substep timestep.
 * @param t         Extrapolation parameter [0,1].
 * @param out_data  Caller buffer (at least 64 bytes) for support data.
 * @param out_fn    Receives the support function pointer.
 * @return true on success, false if shape type is unsupported.
 */
static bool build_support_at_t(const phys_body_t *body,
                                const phys_collider_t *collider,
                                const void *spheres,
                                const void *capsules,
                                const void *boxes,
                                float dt,
                                float t,
                                void *out_data,
                                phys_gjk_support_fn *out_fn) {
    /* Extrapolate pose forward. */
    phys_vec3_t pos = extrapolate_position(body->position, body->linear_vel,
                                            dt, t);
    phys_quat_t rot = extrapolate_orientation(body->orientation,
                                               body->angular_vel, dt, t);

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
 * @brief Sweep-test one pair via forward extrapolation, emit manifold
 *        if collision found.
 *
 * Strategy:
 * 1. Test at t=1 (extrapolated poses). If no overlap, early-out.
 * 2. If overlap at t=0 (current poses), report contact at t=0.
 * 3. Otherwise, bisect [0,1] to find the earliest overlap time.
 * 4. At the TOI, run EPA to get penetration normal+depth+points.
 * 5. Build manifold from the EPA result.
 *
 * @return true if a manifold was emitted, false otherwise.
 */
static bool sweep_pair(uint32_t body_a, uint32_t body_b,
                        const phys_ccd_dynamic_args_t *args,
                        phys_manifold_t *manifold_out) {
    const phys_body_t *ba = &args->bodies[body_a];
    const phys_body_t *bb = &args->bodies[body_b];
    const phys_collider_t *col_a = &args->colliders[body_a];
    const phys_collider_t *col_b = &args->colliders[body_b];
    float dt = args->dt;

    /* --- Pre-filter 1: bounding radii (needed for both filters) --- */
    float r_a = bounding_radius(col_a, args->spheres, args->capsules,
                                 args->boxes);
    float r_b = bounding_radius(col_b, args->spheres, args->capsules,
                                 args->boxes);

    /* --- Pre-filter 2: effective relative speed threshold ---
     * Include angular velocity contribution: surface speed = |ω| × r. */
    float rvx = bb->linear_vel.x - ba->linear_vel.x;
    float rvy = bb->linear_vel.y - ba->linear_vel.y;
    float rvz = bb->linear_vel.z - ba->linear_vel.z;
    float rel_speed_sq = rvx * rvx + rvy * rvy + rvz * rvz;

    float wa = ba->angular_vel.x * ba->angular_vel.x +
               ba->angular_vel.y * ba->angular_vel.y +
               ba->angular_vel.z * ba->angular_vel.z;
    float wb = bb->angular_vel.x * bb->angular_vel.x +
               bb->angular_vel.y * bb->angular_vel.y +
               bb->angular_vel.z * bb->angular_vel.z;
    float ang_speed = sqrtf(wa) * r_a + sqrtf(wb) * r_b;
    float total_speed_sq = rel_speed_sq + ang_speed * ang_speed;
    if (total_speed_sq < CCD_MIN_RELATIVE_SPEED * CCD_MIN_RELATIVE_SPEED)
        return false;

    /* --- Pre-filter 3: swept bounding-sphere overlap --- */
    if (!swept_spheres_overlap(ba, bb, r_a, r_b, dt))
        return false;

    /* Buffers for support data (sized to largest struct). */
    _Alignas(16) uint8_t data_a_buf[64];
    _Alignas(16) uint8_t data_b_buf[64];
    phys_gjk_support_fn fn_a, fn_b;

    /* --- Step 1: Test at t=1 (extrapolated poses) --- */
    if (!build_support_at_t(ba, col_a, args->spheres, args->capsules,
                             args->boxes, dt, 1.0f, data_a_buf, &fn_a))
        return false;
    if (!build_support_at_t(bb, col_b, args->spheres, args->capsules,
                             args->boxes, dt, 1.0f, data_b_buf, &fn_b))
        return false;

    phys_gjk_result_t result;
    bool overlap_at_1 = phys_gjk_intersect(fn_a, data_a_buf,
                                            fn_b, data_b_buf, &result);

    /* --- Step 2: Test at t=0 (current poses) --- */
    if (!build_support_at_t(ba, col_a, args->spheres, args->capsules,
                             args->boxes, dt, 0.0f, data_a_buf, &fn_a))
        return false;
    if (!build_support_at_t(bb, col_b, args->spheres, args->capsules,
                             args->boxes, dt, 0.0f, data_b_buf, &fn_b))
        return false;

    phys_gjk_result_t result_0;
    bool overlap_at_0 = phys_gjk_intersect(fn_a, data_a_buf,
                                            fn_b, data_b_buf, &result_0);

    if (!overlap_at_0 && !overlap_at_1) {
        /* No overlap at either end — check if they cross mid-sweep. */
        float lo = 0.0f, hi = 1.0f;
        bool found = false;
        for (int i = 0; i < CCD_MAX_BISECT_ITERS; i++) {
            float mid = (lo + hi) * 0.5f;
            build_support_at_t(ba, col_a, args->spheres, args->capsules,
                               args->boxes, dt, mid, data_a_buf, &fn_a);
            build_support_at_t(bb, col_b, args->spheres, args->capsules,
                               args->boxes, dt, mid, data_b_buf, &fn_b);

            phys_gjk_result_t mid_result;
            bool mid_overlap = phys_gjk_intersect(fn_a, data_a_buf,
                                                   fn_b, data_b_buf,
                                                   &mid_result);
            if (mid_overlap) {
                found = true;
                result = mid_result;
                hi = mid;
            } else {
                lo = mid;
            }
        }
        if (!found) return false;
    }

    /* --- Step 3: Bisect for earliest TOI --- */
    float toi;
    if (overlap_at_0) {
        /* Already overlapping at start — TOI = 0. */
        toi = 0.0f;
        result = result_0;
    } else if (overlap_at_1 && !overlap_at_0) {
        /* Overlap at t=1 but not t=0 — bisect for earliest TOI. */
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < CCD_MAX_BISECT_ITERS; i++) {
            float mid = (lo + hi) * 0.5f;
            build_support_at_t(ba, col_a, args->spheres, args->capsules,
                               args->boxes, dt, mid, data_a_buf, &fn_a);
            build_support_at_t(bb, col_b, args->spheres, args->capsules,
                               args->boxes, dt, mid, data_b_buf, &fn_b);

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
        /* Re-test at toi for final GJK result. */
        build_support_at_t(ba, col_a, args->spheres, args->capsules,
                           args->boxes, dt, toi, data_a_buf, &fn_a);
        build_support_at_t(bb, col_b, args->spheres, args->capsules,
                           args->boxes, dt, toi, data_b_buf, &fn_b);
        phys_gjk_intersect(fn_a, data_a_buf, fn_b, data_b_buf, &result);
    } else {
        /* Found via mid-sweep bisection — toi already narrowed. */
        toi = 0.5f;
    }

    /* --- Step 4: EPA for penetration info --- */
    if (!result.intersecting) return false;

    /* Rebuild support data at TOI for EPA. */
    build_support_at_t(ba, col_a, args->spheres, args->capsules,
                       args->boxes, dt, toi, data_a_buf, &fn_a);
    build_support_at_t(bb, col_b, args->spheres, args->capsules,
                       args->boxes, dt, toi, data_b_buf, &fn_b);

    bool epa_ok = phys_epa_penetration(fn_a, data_a_buf,
                                        fn_b, data_b_buf, &result);
    if (!epa_ok) return false;

    /* --- Step 5: Build manifold --- */
    phys_manifold_init(manifold_out, body_a, body_b);

    /* Use current material properties. */
    manifold_out->friction = (ba->friction + bb->friction) * 0.5f;
    manifold_out->restitution = (ba->restitution > bb->restitution)
                                 ? ba->restitution : bb->restitution;

    /* Compute contact point in world space at the TOI. */
    phys_vec3_t pos_a_toi = extrapolate_position(ba->position, ba->linear_vel,
                                                  dt, toi);
    phys_vec3_t pos_b_toi = extrapolate_position(bb->position, bb->linear_vel,
                                                  dt, toi);

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
    if (!args->bodies) return 0;
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
        if (!is_dynamic(&args->bodies[a])) continue;
        if (!is_dynamic(&args->bodies[b])) continue;

        /* At least one must have CCD flag. */
        uint32_t flags_a = args->bodies[a].flags;
        uint32_t flags_b = args->bodies[b].flags;
        if (!(flags_a & PHYS_BODY_FLAG_CCD) &&
            !(flags_b & PHYS_BODY_FLAG_CCD)) continue;

        /* Both must be primitive shapes (sphere/box/capsule). */
        if (!is_primitive_shape(args->colliders[a].type)) continue;
        if (!is_primitive_shape(args->colliders[b].type)) continue;

        /* Skip pairs connected by a joint — jointed bodies are
         * constrained together and should not get CCD manifolds
         * (let narrowphase handle them normally). */
        if (args->joints && args->joint_count > 0 &&
            bodies_connected_by_joint(a, b, args->joints,
                                       args->joint_count))
            continue;

        /* Check output buffer capacity. */
        if (*args->manifold_count_out >= args->max_manifolds) break;

        /* Sweep-test this pair. */
        phys_manifold_t *slot = &args->manifolds_out[*args->manifold_count_out];
        if (sweep_pair(a, b, args, slot)) {
            (*args->manifold_count_out)++;
            emitted++;
            /* Mark this pair so narrowphase skips it. */
            if (args->skip_pair_out) args->skip_pair_out[i] = 1;
        }
    }

    return emitted;
}
