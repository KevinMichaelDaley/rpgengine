/**
 * @file cg_solve.c
 * @brief Projected Conjugate Gradient solver with Jacobi preconditioning.
 *
 * Solves A·λ = b subject to λ_min ≤ λ ≤ λ_max.
 *
 * 1 non-static function: phys_cg_solve.
 */

#include "ferrum/physics/solver/cg_solve.h"

#include <math.h>
#include <stdio.h>

/**
 * @brief Sparse matrix-vector multiply: out = A · x.
 */
static void spmv(const cg_sparse_row_t *A_rows, const float *x,
                 float *out, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        float sum = 0.0f;
        const cg_sparse_row_t *row = &A_rows[i];
        for (uint16_t k = 0; k < row->nnz; k++) {
            sum += row->values[k] * x[row->col_indices[k]];
        }
        out[i] = sum;
    }
}

/**
 * @brief Project lambda onto box constraints [lambda_min, lambda_max].
 */
static void project_bounds(float *lambda, const float *lo, const float *hi,
                           uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (lambda[i] < lo[i]) lambda[i] = lo[i];
        if (lambda[i] > hi[i]) lambda[i] = hi[i];
    }
}

/**
 * @brief Project friction lambdas onto the Coulomb friction cone.
 *
 * For each friction row, clamps lambda to [-μ·λ_normal, +μ·λ_normal].
 * This ensures friction forces never exceed the normal force scaled by
 * the friction coefficient, preventing contacts from pulling bodies
 * together through unconstrained tangential impulses.
 *
 * Must be called after project_bounds (which clamps λ_normal ≥ 0).
 */
static void project_friction_cone(float *lambda,
                                  const uint32_t *friction_normal_cg_row,
                                  const float *friction_coeff,
                                  uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t ni = friction_normal_cg_row[i];
        if (ni == UINT32_MAX) continue;
        float limit = friction_coeff[i] * lambda[ni];
        if (limit < 0.0f) limit = 0.0f;
        if (lambda[i] < -limit) lambda[i] = -limit;
        if (lambda[i] >  limit) lambda[i] =  limit;
    }
}

/**
 * @brief Mask residual for active bound constraints.
 *
 * For components at a bound, zero out the residual if the gradient
 * points further into the bound (constraint is active and satisfied).
 */
static void mask_residual(float *r, const float *lambda,
                          const float *lo, const float *hi,
                          uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (lambda[i] <= lo[i] && r[i] < 0.0f) r[i] = 0.0f;
        if (lambda[i] >= hi[i] && r[i] > 0.0f) r[i] = 0.0f;
    }
}

/**
 * @brief Dot product of two vectors.
 */
static float dot_product(const float *a, const float *b, uint32_t n)
{
    float sum = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        sum += a[i] * b[i];
    }
    return sum;
}

uint32_t phys_cg_solve(cg_system_t *sys,
                       uint32_t max_iters,
                       float tolerance)
{
    if (!sys || sys->n == 0) return 0;

    uint32_t n = sys->n;
    float *lambda = sys->lambda;
    float *r = sys->residual;
    float *p = sys->search_dir;
    float *z = sys->z;
    float *Ap = sys->Ap;

    /* Project initial guess onto bounds and friction cone. */
    project_bounds(lambda, sys->lambda_min, sys->lambda_max, n);
    project_friction_cone(lambda, sys->friction_normal_cg_row,
                          sys->friction_coeff, n);

    /* Initial residual: r = b - A·λ */
    spmv(sys->A_rows, lambda, Ap, n);
    for (uint32_t i = 0; i < n; i++) {
        r[i] = sys->rhs[i] - Ap[i];
    }
    mask_residual(r, lambda, sys->lambda_min, sys->lambda_max, n);

    /* Preconditioned residual: z = M⁻¹·r */
    for (uint32_t i = 0; i < n; i++) {
        z[i] = sys->diag_inv[i] * r[i];
    }

    /* Initial search direction: p = z */
    for (uint32_t i = 0; i < n; i++) {
        p[i] = z[i];
    }

    float rz_old = dot_product(r, z, n);

    /* Debug: track gradient magnitude and constraint error per iteration.
     * Only print for the first few solves to avoid flooding. */
    static int cg_solve_dbg_count = 0;
    static int cg_contact_dbg = 0;
    int cg_solve_dbg = 0;
    if (n > 92 && cg_contact_dbg < 3) {
        fprintf(stderr, "[CG-SIZE] solve=%d n=%u\n", cg_solve_dbg_count, n);
        cg_solve_dbg = 1;
    }

    if (cg_solve_dbg) {
        float r_norm = sqrtf(dot_product(r, r, n));
        float rhs_norm = sqrtf(dot_product(sys->rhs, sys->rhs, n));
        fprintf(stderr, "[CG-ITER] solve=%d n=%u rhs_norm=%.6f init_r_norm=%.6f\n",
                cg_solve_dbg_count, n, rhs_norm, r_norm);
        /* Print per-row RHS for joints */
        for (uint32_t i = 0; i < n && i < 40; i++) {
            float diag = (sys->diag_inv[i] > 1e-12f)
                       ? (1.0f / sys->diag_inv[i]) : 0.0f;
            fprintf(stderr, "  row[%u] rhs=%.6f lambda=%.6f lo=%.2f hi=%.2f diag=%.4f\n",
                    i, sys->rhs[i], sys->lambda[i],
                    sys->lambda_min[i], sys->lambda_max[i], diag);
        }
    }

    uint32_t iter;
    for (iter = 0; iter < max_iters; iter++) {
        if (rz_old < tolerance) break;

        /* Ap = A · p */
        spmv(sys->A_rows, p, Ap, n);

        float pAp = dot_product(p, Ap, n);
        if (pAp <= 1e-20f) break;

        float alpha = rz_old / pAp;

        /* Update lambda: λ += α·p, then project. */
        for (uint32_t i = 0; i < n; i++) {
            lambda[i] += alpha * p[i];
        }
        project_bounds(lambda, sys->lambda_min, sys->lambda_max, n);

        /* Friction cone projection: clamp friction lambdas to
         * ±μ·λ_normal.  Must come after project_bounds so that
         * λ_normal ≥ 0 is guaranteed.  Prevents contacts from
         * exerting tangential forces that exceed the Coulomb limit,
         * which would otherwise pull bodies together. */
        project_friction_cone(lambda, sys->friction_normal_cg_row,
                              sys->friction_coeff, n);

        /* Recompute residual after projection (projection breaks
         * the CG recurrence r -= α·Ap, so full recomputation is
         * needed for correctness). */
        spmv(sys->A_rows, lambda, Ap, n);
        for (uint32_t i = 0; i < n; i++) {
            r[i] = sys->rhs[i] - Ap[i];
        }
        mask_residual(r, lambda, sys->lambda_min, sys->lambda_max, n);

        /* Preconditioned residual. */
        for (uint32_t i = 0; i < n; i++) {
            z[i] = sys->diag_inv[i] * r[i];
        }

        float rz_new = dot_product(r, z, n);

        if (cg_solve_dbg) {
            float r_norm = sqrtf(dot_product(r, r, n));
            float grad_mag = sqrtf(rz_new); /* |M⁻¹r| proxy */
            fprintf(stderr, "[CG-ITER] solve=%d iter=%u r_norm=%.6f grad=%.6f alpha=%.6f rz=%.6e\n",
                    cg_solve_dbg_count, iter, r_norm, grad_mag, alpha, rz_new);
        }

        if (fabsf(rz_old) < 1e-20f) break;

        float beta = rz_new / rz_old;

        /* Update search direction: p = z + β·p */
        for (uint32_t i = 0; i < n; i++) {
            p[i] = z[i] + beta * p[i];
        }

        rz_old = rz_new;
    }

    if (cg_solve_dbg) {
        fprintf(stderr, "[CG-ITER] solve=%d DONE iters=%u final_rz=%.6e\n",
                cg_solve_dbg_count, iter, rz_old);
        /* Print final lambda for each row. */
        for (uint32_t i = 0; i < n && i < 40; i++) {
            fprintf(stderr, "  row[%u] final_lambda=%.6f rhs=%.6f ratio=%.4f\n",
                    i, lambda[i], sys->rhs[i],
                    (fabsf(sys->rhs[i]) > 1e-12f)
                        ? lambda[i] / sys->rhs[i] : 0.0f);
        }
        if (n > 92) cg_contact_dbg++;
    }
    cg_solve_dbg_count++;

    return iter;
}
