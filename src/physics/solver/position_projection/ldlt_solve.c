/**
 * @file ldlt_solve.c
 * @brief Dense LDL^T factorization and solve for SPD systems.
 *
 * Used by position projection to solve A*lambda = -Phi(q) per island.
 * For small islands this dense approach is efficient; larger islands
 * could use sparse factorization in the future.
 */

#include "ferrum/physics/position_projection.h"

#include <math.h>
#include <stddef.h>

/** Minimum diagonal value to consider non-singular. */
#define LDLT_EPSILON 1e-12f

bool phys_dense_ldlt_solve(float *A, float *b, float *x, uint32_t n)
{
    if (!A || !b || !x || n == 0) {
        return false;
    }

    /* In-place LDL^T factorization of symmetric matrix A (row-major).
     *
     * After factorization, the lower triangle of A holds L (unit lower)
     * and the diagonal of A holds D.
     *
     * L[i][j] = (A[i][j] - sum_{k<j} L[i][k]*D[k]*L[j][k]) / D[j]
     * D[i]    = A[i][i] - sum_{k<i} L[i][k]^2 * D[k]
     */
    for (uint32_t j = 0; j < n; j++) {
        /* Compute D[j]. */
        float dj = A[j * n + j];
        for (uint32_t k = 0; k < j; k++) {
            float ljk = A[j * n + k];
            dj -= ljk * ljk * A[k * n + k];
        }
        A[j * n + j] = dj;

        if (fabsf(dj) < LDLT_EPSILON) {
            /* Degenerate row: treat as zero. Set column to zero. */
            for (uint32_t i = j + 1; i < n; i++) {
                A[i * n + j] = 0.0f;
            }
            continue;
        }

        /* Compute L[i][j] for i > j. */
        float inv_dj = 1.0f / dj;
        for (uint32_t i = j + 1; i < n; i++) {
            float lij = A[i * n + j];
            for (uint32_t k = 0; k < j; k++) {
                lij -= A[i * n + k] * A[k * n + k] * A[j * n + k];
            }
            A[i * n + j] = lij * inv_dj;
        }
    }

    /* Forward substitution: L * y = b (y stored in x). */
    for (uint32_t i = 0; i < n; i++) {
        float sum = b[i];
        for (uint32_t k = 0; k < i; k++) {
            sum -= A[i * n + k] * x[k];
        }
        x[i] = sum;
    }

    /* Diagonal solve: D * z = y (z stored in x). */
    for (uint32_t i = 0; i < n; i++) {
        float di = A[i * n + i];
        if (fabsf(di) < LDLT_EPSILON) {
            x[i] = 0.0f;
        } else {
            x[i] = x[i] / di;
        }
    }

    /* Backward substitution: L^T * x = z. */
    for (uint32_t i = n; i > 0; i--) {
        uint32_t idx = i - 1;
        float val = x[idx];
        for (uint32_t k = idx + 1; k < n; k++) {
            val -= A[k * n + idx] * x[k];
        }
        x[idx] = val;
    }

    return true;
}
