#ifndef FERRUM_PHYSICS_SOLVER_CG_TYPES_H
#define FERRUM_PHYSICS_SOLVER_CG_TYPES_H

/** @file
 * @brief Sparse Conjugate Gradient solver types for joint constraints.
 *
 * The CG solver assembles the full constraint-space system matrix
 * A = J·M⁻¹·Jᵀ + diag(regularization) and solves for all joint
 * constraint impulses simultaneously, eliminating the sequential
 * coupling artifacts of Gauss-Seidel.
 *
 * Sparse CSR (Compressed Sparse Row) storage is used for A since
 * each row only couples to rows that share a body.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum non-zeros per row in the sparse system matrix.
 *  Each body participates in at most ~4 joints with up to 6 active
 *  rows each, giving ~24 coupled rows.  32 provides headroom. */
#define CG_MAX_NNZ_PER_ROW 32

/** Maximum total constraint rows the CG solver can handle per island.
 *  128 bodies × 9 rows/joint = 1152 theoretical max; 512 is generous
 *  for practical ragdoll islands (typically ~60 rows). */
#define CG_MAX_ROWS 512

/**
 * @brief One row of the sparse system matrix A (CSR format).
 *
 * Fixed-size arrays avoid dynamic allocation.  If a row exceeds
 * CG_MAX_NNZ_PER_ROW non-zeros, assembly truncates (falls back
 * to GS for that island).
 */
typedef struct cg_sparse_row {
    float    values[CG_MAX_NNZ_PER_ROW];     /**< Non-zero values. */
    uint16_t col_indices[CG_MAX_NNZ_PER_ROW]; /**< Column indices. */
    uint16_t nnz;                              /**< Number of non-zeros. */
} cg_sparse_row_t;

/**
 * @brief Workspace for the sparse projected CG solver.
 *
 * All arrays are arena-allocated; no dynamic memory.
 * The system solves: A · Δλ = b, subject to λ_min ≤ λ ≤ λ_max.
 *
 * @par Ownership
 * Arena-allocated.  Caller owns the arena; all pointers are
 * invalidated when the arena is reset.
 */
typedef struct cg_system {
    cg_sparse_row_t *A_rows;   /**< Sparse A matrix rows [n]. */
    float *diag_inv;           /**< Jacobi preconditioner: 1/A[i][i] [n]. */
    float *rhs;                /**< Right-hand side b [n]. */
    float *lambda;             /**< Solution vector (accumulated impulse) [n]. */
    float *residual;           /**< r = b - A·λ [n]. */
    float *search_dir;         /**< CG search direction p [n]. */
    float *z;                  /**< Preconditioned residual M⁻¹·r [n]. */
    float *Ap;                 /**< Scratch: A·p [n]. */
    float *lambda_min;         /**< Per-row lower bound [n]. */
    float *lambda_max;         /**< Per-row upper bound [n]. */

    /** Maps CG row index → (constraint_index, row_within_constraint). */
    uint32_t *row_constraint;  /**< Constraint index for each CG row [n]. */
    uint8_t  *row_sub;         /**< Row-within-constraint for each CG row [n]. */

    uint32_t n;                /**< System size (total active joint rows). */
    uint8_t  overflow;         /**< 1 if NNZ overflow occurred during assembly. */
} cg_system_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_SOLVER_CG_TYPES_H */
