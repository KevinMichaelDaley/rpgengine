---
id: rpg-7qjh
status: closed
deps: [rpg-tn1j]
links: []
created: 2026-02-07T19:44:41Z
type: task
priority: 1
assignee: KMD
parent: rpg-m9nw
---
# Sparse stabilization: Schur complement system matrix A = J M^-1 J^T

Compute the symmetric positive-definite system matrix A = J * M^-1 * J^T per island. Use the sparsity structure (each constraint connects exactly 2 bodies) to compute only non-zero entries. Store in a format suitable for sparse factorization (CSR or dense-per-island for small islands).

## Acceptance Criteria

A matrix correct for 2-body, 3-body stack, and 10-body chain scenarios; symmetry verified; positive-definiteness verified via diagonal dominance or Cholesky success

