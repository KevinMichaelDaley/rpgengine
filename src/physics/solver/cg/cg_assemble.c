/**
 * @file cg_assemble.c
 * @brief Assemble sparse system matrix A = J·M⁻¹·Jᵀ + regularization.
 *
 * 1 non-static function: phys_cg_assemble.
 */

#include "ferrum/physics/solver/cg_solve.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/math/vec3.h"

#include <math.h>
#include <string.h>

/**
 * @brief Add a value to a sparse row at the given column.
 *
 * If the column already exists, accumulates.  Otherwise appends.
 * Returns false if the row is full (NNZ overflow).
 */
static bool sparse_row_add(cg_sparse_row_t *row,
                           uint16_t col, float value)
{
    /* Check for existing entry. */
    for (uint16_t i = 0; i < row->nnz; i++) {
        if (row->col_indices[i] == col) {
            row->values[i] += value;
            return true;
        }
    }
    /* Append new entry. */
    if (row->nnz >= CG_MAX_NNZ_PER_ROW) return false;
    row->col_indices[row->nnz] = col;
    row->values[row->nnz] = value;
    row->nnz++;
    return true;
}

/**
 * @brief Compute the 6-element Jacobian block for a row on one body.
 *
 * out[0..2] = J_linear (J_va or J_vb)
 * out[3..5] = J_angular (J_wa or J_wb)
 */
static void get_jacobian_block(const phys_jacobian_row_t *row,
                               int body_side, float out[6])
{
    if (body_side == 0) {
        out[0] = row->J_va.x; out[1] = row->J_va.y; out[2] = row->J_va.z;
        out[3] = row->J_wa.x; out[4] = row->J_wa.y; out[5] = row->J_wa.z;
    } else {
        out[0] = row->J_vb.x; out[1] = row->J_vb.y; out[2] = row->J_vb.z;
        out[3] = row->J_wb.x; out[4] = row->J_wb.y; out[5] = row->J_wb.z;
    }
}

/**
 * @brief Compute J_i · M⁻¹_k · J_j^T for one shared body k.
 *
 * The 6×6 block-diagonal inverse mass for body k is:
 *   diag(inv_mass, inv_mass, inv_mass) for linear,
 *   inv_inertia_world (3×3) for angular.
 */
static float compute_coupling(const float Ji[6], const float Jj[6],
                              float inv_mass,
                              const phys_mat3_t *inv_I)
{
    /* Linear contribution: Ji_lin · (inv_mass · I) · Jj_lin */
    float lin = inv_mass * (Ji[0]*Jj[0] + Ji[1]*Jj[1] + Ji[2]*Jj[2]);

    /* Angular contribution: Ji_ang · inv_I · Jj_ang */
    phys_vec3_t Jj_ang = {Jj[3], Jj[4], Jj[5]};
    phys_vec3_t iI_Jj = phys_mat3_mul_vec3(inv_I, Jj_ang);
    float ang = Ji[3]*iI_Jj.x + Ji[4]*iI_Jj.y + Ji[5]*iI_Jj.z;

    return lin + ang;
}

void phys_cg_assemble(cg_system_t *sys,
                      const phys_island_t *island,
                      const phys_constraint_t *constraints,
                      const phys_body_t *bodies,
                      const phys_mat3_t *inv_inertia_world,
                      const phys_velocity_t *velocities,
                      uint32_t body_count,
                      float dt)
{
    if (!sys || !island || !constraints || !bodies || !velocities) {
        if (sys) sys->n = 0;
        return;
    }

    float inv_dt = (dt > 1e-12f) ? (1.0f / dt) : 0.0f;
    sys->n = 0;
    sys->overflow = 0;

    /* ---- Pass 1: Collect all joint rows into the CG system ---- */

    /* Temporary: map (constraint_index, row_index) → CG row index.
     * We also need to know which CG rows share a body for coupling.
     *
     * For each CG row, store the body_a, body_b it references. */
    uint32_t cg_body_a[CG_MAX_ROWS];
    uint32_t cg_body_b[CG_MAX_ROWS];

    for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
        uint32_t c_idx = island->constraint_indices[ci];
        const phys_constraint_t *c = &constraints[c_idx];

        /* Include both joints and contacts in the CG system. */
        if (c->body_a >= body_count || c->body_b >= body_count) continue;

        for (uint8_t r = 0; r < c->row_count; r++) {
            if (sys->n >= CG_MAX_ROWS) {
                sys->overflow = 1;
                return;
            }
            uint32_t idx = sys->n;
            sys->row_constraint[idx] = c_idx;
            sys->row_sub[idx] = r;
            cg_body_a[idx] = c->body_a;
            cg_body_b[idx] = c->body_b;

            /* Store accumulated lambda for RHS compliance feedback,
             * but initialize CG solution to zero — we solve for
             * the incremental Δλ, not the total. */
            sys->lambda[idx] = c->rows[r].lambda;
            sys->lambda_min[idx] = c->rows[r].lambda_min;
            sys->lambda_max[idx] = c->rows[r].lambda_max;

            sys->n++;
        }
    }

    if (sys->n == 0) return;

    /* ---- Pass 2: Build sparse A matrix ---- */

    /* Zero all sparse rows. */
    memset(sys->A_rows, 0, sys->n * sizeof(cg_sparse_row_t));

    /* For each pair of CG rows (i, j) that share a body, compute
     * A[i][j] = Σ_shared_bodies J_i_k · M⁻¹_k · J_j_k^T.
     *
     * Optimization: iterate over bodies, find all CG rows referencing
     * each body, then fill in all pairwise couplings.  But with at
     * most ~60 rows for a ragdoll, the O(n²) approach is fine. */
    for (uint32_t i = 0; i < sys->n; i++) {
        const phys_constraint_t *ci_c = &constraints[sys->row_constraint[i]];
        const phys_jacobian_row_t *ri = &ci_c->rows[sys->row_sub[i]];

        for (uint32_t j = i; j < sys->n; j++) {
            const phys_constraint_t *cj_c = &constraints[sys->row_constraint[j]];
            const phys_jacobian_row_t *rj = &cj_c->rows[sys->row_sub[j]];

            float coupling = 0.0f;

            /* Check body_a of row i against both bodies of row j. */
            uint32_t bi_a = cg_body_a[i], bi_b = cg_body_b[i];
            uint32_t bj_a = cg_body_a[j], bj_b = cg_body_b[j];

            /* For each shared body, compute the coupling. */
            if (bi_a == bj_a && bi_a < body_count) {
                float Ji[6], Jj[6];
                get_jacobian_block(ri, 0, Ji);
                get_jacobian_block(rj, 0, Jj);
                const phys_mat3_t *inv_I = inv_inertia_world
                    ? &inv_inertia_world[bi_a] : NULL;
                if (inv_I) {
                    coupling += compute_coupling(Ji, Jj,
                        bodies[bi_a].inv_mass, inv_I);
                }
            }
            if (bi_a == bj_b && bi_a < body_count) {
                float Ji[6], Jj[6];
                get_jacobian_block(ri, 0, Ji);
                get_jacobian_block(rj, 1, Jj);
                const phys_mat3_t *inv_I = inv_inertia_world
                    ? &inv_inertia_world[bi_a] : NULL;
                if (inv_I) {
                    coupling += compute_coupling(Ji, Jj,
                        bodies[bi_a].inv_mass, inv_I);
                }
            }
            if (bi_b == bj_a && bi_b < body_count && bi_b != bi_a) {
                float Ji[6], Jj[6];
                get_jacobian_block(ri, 1, Ji);
                get_jacobian_block(rj, 0, Jj);
                const phys_mat3_t *inv_I = inv_inertia_world
                    ? &inv_inertia_world[bi_b] : NULL;
                if (inv_I) {
                    coupling += compute_coupling(Ji, Jj,
                        bodies[bi_b].inv_mass, inv_I);
                }
            }
            if (bi_b == bj_b && bi_b < body_count) {
                float Ji[6], Jj[6];
                get_jacobian_block(ri, 1, Ji);
                get_jacobian_block(rj, 1, Jj);
                const phys_mat3_t *inv_I = inv_inertia_world
                    ? &inv_inertia_world[bi_b] : NULL;
                if (inv_I) {
                    coupling += compute_coupling(Ji, Jj,
                        bodies[bi_b].inv_mass, inv_I);
                }
            }

            if (fabsf(coupling) < 1e-20f && i != j) continue;

            /* Diagonal: add regularization terms. */
            if (i == j) {
                /* XPBD compliance: α/h² */
                float alpha;
                if (ri->flags & PHYS_ROW_FLAG_DRIVE) {
                    alpha = ci_c->drive_compliance;
                } else if ((ri->flags & PHYS_ROW_FLAG_ANGULAR) &&
                           ci_c->angular_compliance > 0.0f) {
                    alpha = ci_c->angular_compliance;
                } else {
                    alpha = ci_c->compliance;
                }
                coupling += alpha * inv_dt * inv_dt;

                /* Damping: γ/h = β·α/h² (timestep-independent).
                 * β is the dimensionless damping ratio from the joint. */
                coupling += ci_c->joint_damping * alpha * inv_dt * inv_dt;

                /* Geometric stiffness for angular rows:
                 * h²·Σ_{j≠i} |λ_j|·(1 - (n_i·n_j)²) */
                if (ri->flags & PHYS_ROW_FLAG_ANGULAR) {
                    float k_geo = 0.0f;
                    for (uint32_t s = 0; s < ci_c->row_count; s++) {
                        if (s == sys->row_sub[i]) continue;
                        if (!(ci_c->rows[s].flags & PHYS_ROW_FLAG_ANGULAR))
                            continue;
                        float dot = vec3_dot(ri->J_wb, ci_c->rows[s].J_wb);
                        k_geo += fabsf(ci_c->rows[s].lambda) *
                                 (1.0f - dot * dot);
                    }
                    coupling += dt * dt * k_geo;
                }
            }

            /* Store in sparse row (symmetric: store both A[i][j] and A[j][i]). */
            if (!sparse_row_add(&sys->A_rows[i], (uint16_t)j, coupling)) {
                sys->overflow = 1;
            }
            if (i != j) {
                if (!sparse_row_add(&sys->A_rows[j], (uint16_t)i, coupling)) {
                    sys->overflow = 1;
                }
            }
        }
    }

    /* ---- Pass 3: Build RHS and preconditioner ---- */

    for (uint32_t i = 0; i < sys->n; i++) {
        const phys_constraint_t *c = &constraints[sys->row_constraint[i]];
        const phys_jacobian_row_t *row = &c->rows[sys->row_sub[i]];

        /* J·v: relative velocity along constraint direction. */
        uint32_t ba = c->body_a, bb = c->body_b;
        float jv = 0.0f;
        if (ba < body_count) {
            jv += vec3_dot(row->J_va, velocities[ba].linear)
                + vec3_dot(row->J_wa, velocities[ba].angular);
        }
        if (bb < body_count) {
            jv += vec3_dot(row->J_vb, velocities[bb].linear)
                + vec3_dot(row->J_wb, velocities[bb].angular);
        }

        /* Position error bias. */
        float C_i = row->bias;
        float erp = (row->flags & PHYS_ROW_FLAG_ANGULAR) ? 0.1f : 0.6f;

        /* Compliance feedback term. */
        float alpha;
        if (row->flags & PHYS_ROW_FLAG_DRIVE) {
            alpha = c->drive_compliance;
        } else if ((row->flags & PHYS_ROW_FLAG_ANGULAR) &&
                   c->angular_compliance > 0.0f) {
            alpha = c->angular_compliance;
        } else {
            alpha = c->compliance;
        }

        /* RHS = -(J·v + erp·C/h + (α/h²)·λ_warmstart)
         * The compliance feedback α/h² must match the diagonal
         * regularization term to satisfy the XPBD update rule. */
        sys->rhs[i] = -(jv + erp * C_i * inv_dt
                        + alpha * inv_dt * inv_dt * sys->lambda[i]);

        /* Jacobi preconditioner: 1 / A[i][i]. */
        float diag = 0.0f;
        for (uint16_t k = 0; k < sys->A_rows[i].nnz; k++) {
            if (sys->A_rows[i].col_indices[k] == (uint16_t)i) {
                diag = sys->A_rows[i].values[k];
                break;
            }
        }
        sys->diag_inv[i] = (diag > 1e-12f) ? (1.0f / diag) : 0.0f;
    }

    /* Zero lambda so CG solves for the incremental Δλ, not total.
     * The accumulated lambda was already used for the RHS compliance
     * feedback term above. */
    for (uint32_t i = 0; i < sys->n; i++) {
        sys->lambda[i] = 0.0f;
    }
}
