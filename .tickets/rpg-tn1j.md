---
id: rpg-tn1j
status: open
deps: []
links: []
created: 2026-02-07T19:44:41Z
type: task
priority: 1
assignee: KMD
parent: rpg-m9nw
---
# Sparse stabilization: Constraint Jacobian assembly per island

Build the per-island Jacobian matrix J and constraint violation vector Phi(q) from contact manifolds. Each constraint row contributes two non-zero blocks to J. Output: J (sparse CSR or block-sparse), Phi vector, body-to-island mapping. Reuse existing constraint data from Stage 9 (constraint_build) where possible.

## Acceptance Criteria

Jacobian and Phi correctly assembled for single-island and multi-island scenarios; unit tests for 2-body contact, 3-body stack, static-dynamic pair

