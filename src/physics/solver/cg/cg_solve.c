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

    /* Project initial guess onto bounds. */
    project_bounds(lambda, sys->lambda_min, sys->lambda_max, n);

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
        if (fabsf(rz_old) < 1e-20f) break;

        float beta = rz_new / rz_old;

        /* Update search direction: p = z + β·p */
        for (uint32_t i = 0; i < n; i++) {
            p[i] = z[i] + beta * p[i];
        }

        rz_old = rz_new;
    }

    return iter;
}
