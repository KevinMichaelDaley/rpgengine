/**
 * @file joint_constraint.c
 * @brief Convert built joint rows into solver-compatible phys_constraint_t.
 *
 * Packs joint Jacobian rows into one or two constraints (max 3 rows
 * each), with is_joint=1 so solvers skip friction cone logic.
 */

#include "ferrum/physics/joint.h"
#include "ferrum/physics/constraint.h"

#include <string.h>

/**
 * @brief Classify joint as structural (2) or animation (1).
 *
 * Structural joints represent hard physical connections (ball, hinge,
 * lock, distance, cone_twist).  Animation joints represent soft goals
 * (copy_rotation, limit_rotation, limit_position, aim, IK).
 * The solver processes animation constraints first so that structural
 * joints and contacts can override them.
 */
static uint8_t joint_priority(uint8_t type) {
    switch (type) {
    case PHYS_JOINT_COPY_ROTATION:
    case PHYS_JOINT_LIMIT_ROTATION:
    case PHYS_JOINT_LIMIT_POSITION:
    case PHYS_JOINT_AIM:
    case PHYS_JOINT_IK:
        return 1;  /* Animation (soft, solved first). */
    default:
        return 2;  /* Structural (hard, solved second). */
    }
}

/**
 * @brief Fill one constraint from a slice of joint rows.
 *
 * @param c            Output constraint.
 * @param joint        Source joint.
 * @param row_start    First row index in joint->rows to copy.
 * @param row_end      One-past-last row index.
 * @param solver_mode  Solver mode (0=TGS, 1=XPBD).
 */
static void fill_constraint(struct phys_constraint *c,
                            const phys_joint_t *joint,
                            uint8_t row_start,
                            uint8_t row_end,
                            uint8_t solver_mode) {
    memset(c, 0, sizeof(*c));
    c->body_a       = joint->body_a;
    c->body_b       = joint->body_b;
    c->manifold_idx = UINT32_MAX;  /* No manifold back-reference. */
    c->point_idx    = 0;
    c->solver_mode  = solver_mode;
    c->is_joint     = joint_priority(joint->type);
    c->friction     = 0.0f;
    c->penetration  = 0.0f;
    c->compliance        = joint->compliance;
    c->joint_damping     = joint->damping;
    c->drive_compliance  = joint->drive_compliance;

    uint8_t count = row_end - row_start;
    if (count > PHYS_MAX_CONSTRAINT_ROWS) {
        count = PHYS_MAX_CONSTRAINT_ROWS;
    }
    c->row_count = count;

    for (uint8_t i = 0; i < count; ++i) {
        c->rows[i] = joint->rows[row_start + i];
    }
}

uint32_t phys_joint_build_constraints(const phys_joint_t *joint,
                                      struct phys_constraint *out,
                                      uint32_t max_out,
                                      uint8_t solver_mode) {
    if (!joint || !out || max_out == 0 || joint->row_count == 0
        || joint->broken) {
        return 0;
    }

    if (joint->row_count <= PHYS_MAX_CONSTRAINT_ROWS) {
        /* Fits in a single constraint. */
        fill_constraint(&out[0], joint, 0, joint->row_count, solver_mode);
        return 1;
    }

    /* Split: first 3 rows in constraint 0, remainder in constraint 1. */
    fill_constraint(&out[0], joint, 0, PHYS_MAX_CONSTRAINT_ROWS, solver_mode);

    if (max_out < 2) {
        return 1;  /* No room for second constraint. */
    }

    fill_constraint(&out[1], joint,
                    PHYS_MAX_CONSTRAINT_ROWS, joint->row_count,
                    solver_mode);
    return 2;
}
