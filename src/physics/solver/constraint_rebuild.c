/**
 * @file constraint_rebuild.c
 * @brief Constraint rebuild for inter-iteration Jacobian updates.
 *
 * 3 non-static functions:
 *   1. phys_rebuild_island_joint_constraints
 *   2. phys_rebuild_island_contact_constraints
 *   3. phys_rebuild_island_all_constraints
 */

#include "ferrum/physics/constraint_rebuild.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/* ── Joint lambda preservation helpers ──────────────────────────── */

/**
 * @brief Number of rows whose lambdas are stable across rebuilds.
 *
 * Limit joints change row count based on active limits, so their
 * lambdas are not preserved.  Cone-twist preserves only the 3
 * positional rows (angular limit rows are volatile).
 */
static uint8_t stable_joint_lambda_rows(const phys_joint_t *joint)
{
    if (!joint) {
        return 0;
    }

    switch (joint->type) {
    case PHYS_JOINT_LIMIT_ROTATION:
    case PHYS_JOINT_LIMIT_POSITION:
        return 0;
    case PHYS_JOINT_CONE_TWIST:
        /* Preserve all row lambdas including angular limits.
         * The geometric stiffness correction (K_geo) in the coupled
         * solver prevents the energy injection that warm-started angular
         * lambdas would otherwise cause by overshooting the spherical
         * constraint manifold. */
        return joint->row_count;
    default:
        return joint->row_count;
    }
}

void phys_rebuild_joint_by_type(phys_joint_t *j,
                                const phys_body_t *bodies,
                                uint32_t body_count,
                                float dt)
{
    if (j->body_a >= body_count || j->body_b >= body_count) {
        return;
    }

    switch (j->type) {
    case PHYS_JOINT_DISTANCE:
        phys_joint_build_distance(j, &bodies[j->body_a],
                                   &bodies[j->body_b], dt);
        break;
    case PHYS_JOINT_BALL:
        phys_joint_build_ball(j, &bodies[j->body_a],
                              &bodies[j->body_b], dt);
        break;
    case PHYS_JOINT_HINGE:
        phys_joint_build_hinge(j, &bodies[j->body_a],
                               &bodies[j->body_b], dt);
        break;
    case PHYS_JOINT_LOCK:
        phys_joint_build_lock(j, &bodies[j->body_a],
                              &bodies[j->body_b], dt);
        break;
    case PHYS_JOINT_COPY_ROTATION:
        phys_joint_build_copy_rotation(j, &bodies[j->body_a],
                                        &bodies[j->body_b], dt);
        break;
    case PHYS_JOINT_LIMIT_ROTATION:
        phys_joint_build_limit_rotation(j, &bodies[j->body_a],
                                         &bodies[j->body_b], dt);
        break;
    case PHYS_JOINT_LIMIT_POSITION:
        phys_joint_build_limit_position(j, &bodies[j->body_a],
                                         &bodies[j->body_b], dt);
        break;
    case PHYS_JOINT_AIM:
        phys_joint_build_aim(j, &bodies[j->body_a],
                             &bodies[j->body_b], dt);
        break;
    case PHYS_JOINT_IK:
        if (j->ik_target_body != UINT32_MAX && j->ik_target_body < body_count) {
            j->ik_target_pos = bodies[j->ik_target_body].position;
        }
        if (j->ik_ee_body < body_count) {
            phys_joint_build_ik(j, &bodies[j->body_a],
                                &bodies[j->body_b],
                                &bodies[j->ik_ee_body], dt);
        }
        break;
    case PHYS_JOINT_CONE_TWIST:
        phys_joint_build_cone_twist(j, &bodies[j->body_a],
                                    &bodies[j->body_b], dt);
        break;
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void phys_rebuild_island_joint_constraints(
    const phys_island_t *island,
    const phys_constraint_rebuild_args_t *args)
{
    if (!island || !args || !args->constraints || !args->bodies) {
        return;
    }

    for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
        uint32_t c_idx = island->constraint_indices[ci];
        if (c_idx >= args->constraint_count) continue;
        phys_constraint_t *c = &args->constraints[c_idx];
        if (!c->is_joint) continue;

        if (!args->constraint_joint_indices) continue;
        uint32_t ji = args->constraint_joint_indices[c_idx];
        if (ji >= args->joint_count) continue;

        phys_joint_t *j = &args->joints[ji];

        /* Save accumulated lambdas for warm-starting. */
        float saved_lambda[PHYS_JOINT_MAX_ROWS] = {0};
        uint8_t saved_rows = 0;
        uint8_t stable_rows = stable_joint_lambda_rows(j);
        for (uint8_t r = 0; r < c->row_count &&
                            r < stable_rows &&
                            saved_rows < PHYS_JOINT_MAX_ROWS; ++r) {
            saved_lambda[saved_rows++] = c->rows[r].lambda;
        }

        /* Check for a second constraint belonging to the same joint
         * (e.g. hinge = 3 pos rows + 2 angular rows → 2 constraints). */
        uint32_t next_idx = UINT32_MAX;
        if (ci + 1 < island->constraint_count) {
            next_idx = island->constraint_indices[ci + 1];
            if (next_idx < args->constraint_count &&
                args->constraints[next_idx].is_joint &&
                args->constraint_joint_indices[next_idx] == ji) {
                phys_constraint_t *next_c = &args->constraints[next_idx];
                for (uint8_t r = 0; r < next_c->row_count &&
                                    saved_rows < stable_rows &&
                                    saved_rows < PHYS_JOINT_MAX_ROWS; ++r) {
                    saved_lambda[saved_rows++] = next_c->rows[r].lambda;
                }
            } else {
                next_idx = UINT32_MAX;
            }
        }

        /* Rebuild joint rows from current body positions. */
        phys_rebuild_joint_by_type(j, args->bodies, args->body_count, args->dt);

        /* Pack rebuilt rows into constraint(s). */
        phys_constraint_t tmp[2];
        uint32_t written = phys_joint_build_constraints(
            j, tmp, 2, c->solver_mode);

        /* Restore saved lambdas. */
        uint8_t restore_idx = 0;
        for (uint32_t wi = 0; wi < written; ++wi) {
            for (uint8_t r = 0; r < tmp[wi].row_count &&
                                restore_idx < saved_rows; ++r) {
                tmp[wi].rows[r].lambda = saved_lambda[restore_idx++];
            }
        }

        if (written >= 1) {
            *c = tmp[0];
        }
        if (written >= 2 && ci + 1 < island->constraint_count) {
            if (next_idx < args->constraint_count &&
                args->constraints[next_idx].is_joint &&
                args->constraint_joint_indices[next_idx] == ji) {
                args->constraints[next_idx] = tmp[1];
                ci++; /* Skip the second constraint in the loop. */
            }
        }
    }
}

void phys_rebuild_island_contact_constraints(
    const phys_island_t *island,
    const phys_constraint_rebuild_args_t *args)
{
    if (!island || !args || !args->constraints || !args->bodies) {
        return;
    }
    if (!args->manifolds || args->manifold_count == 0) {
        return;
    }

    for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
        uint32_t c_idx = island->constraint_indices[ci];
        if (c_idx >= args->constraint_count) continue;
        phys_constraint_t *c = &args->constraints[c_idx];

        /* Skip joint constraints — handled by joint rebuild. */
        if (c->is_joint) continue;

        /* Look up the manifold and contact point. */
        uint32_t mi = c->manifold_idx;
        if (mi >= args->manifold_count) continue;
        const phys_manifold_t *m = &args->manifolds[mi];

        uint8_t pi = c->point_idx;
        if (pi >= m->point_count) continue;
        const phys_contact_point_t *cp = &m->points[pi];

        uint32_t ba = c->body_a;
        uint32_t bb = c->body_b;
        if (ba >= args->body_count || bb >= args->body_count) continue;

        const phys_body_t *body_a = &args->bodies[ba];
        const phys_body_t *body_b = &args->bodies[bb];

        /* Save accumulated lambdas for warm-starting. */
        float lambdas[PHYS_MAX_CONSTRAINT_ROWS];
        for (uint8_t r = 0; r < c->row_count && r < PHYS_MAX_CONSTRAINT_ROWS; ++r) {
            lambdas[r] = c->rows[r].lambda;
        }
        uint8_t old_row_count = c->row_count;

        /* Recompute world-space contact point from local coordinates
         * and updated body transforms. */
        phys_vec3_t world_a = vec3_add(body_a->position,
            quat_rotate_vec3(body_a->orientation, cp->local_a));
        phys_vec3_t world_b = vec3_add(body_b->position,
            quat_rotate_vec3(body_b->orientation, cp->local_b));

        /* Recompute penetration along the original contact normal.
         * Positive = overlap (A moved into B along normal). */
        float new_pen = vec3_dot(cp->normal,
                                  vec3_sub(world_a, world_b));

        /* Build an updated contact point with new geometry. */
        phys_contact_point_t updated_cp = *cp;
        updated_cp.point_world = vec3_scale(
            vec3_add(world_a, world_b), 0.5f);
        updated_cp.penetration = new_pen;

        /* Save metadata that build_contact overwrites. */
        uint8_t solver_mode = c->solver_mode;
        uint8_t is_joint = c->is_joint;
        float compliance = c->compliance;
        float joint_damping = c->joint_damping;
        uint32_t manifold_idx = c->manifold_idx;
        uint8_t point_idx = c->point_idx;

        /* Rebuild the constraint from the updated contact. */
        phys_constraint_build_contact(c, body_a, body_b,
                                       &updated_cp, c->friction,
                                       m->restitution,
                                       args->dt, args->baumgarte,
                                       args->slop);

        /* Restore metadata and body indices (build_contact may reorder). */
        c->body_a = ba;
        c->body_b = bb;
        c->solver_mode = solver_mode;
        c->is_joint = is_joint;
        c->compliance = compliance;
        c->joint_damping = joint_damping;
        c->manifold_idx = manifold_idx;
        c->point_idx = point_idx;

        /* Recompute effective mass with current inverse inertia. */
        if (args->inv_inertia_world) {
            for (uint8_t r = 0; r < c->row_count; ++r) {
                c->rows[r].effective_mass = phys_compute_effective_mass(
                    &c->rows[r],
                    body_a->inv_mass, &args->inv_inertia_world[ba],
                    body_b->inv_mass, &args->inv_inertia_world[bb]);
            }
        }

        /* Restore accumulated lambdas. */
        for (uint8_t r = 0; r < c->row_count && r < old_row_count; ++r) {
            c->rows[r].lambda = lambdas[r];
        }
    }
}

void phys_rebuild_island_all_constraints(
    const phys_island_t *island,
    const phys_constraint_rebuild_args_t *args)
{
    phys_rebuild_island_joint_constraints(island, args);
    phys_rebuild_island_contact_constraints(island, args);
}
