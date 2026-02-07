---
id: rpg-pjrp
status: closed
deps: [rpg-7qjh]
links: []
created: 2026-02-07T19:44:41Z
type: task
priority: 1
assignee: KMD
parent: rpg-m9nw
---
# Sparse stabilization: Sparse LDL^T solver

Implement a sparse LDL^T (or Cholesky) factorization and solve for the system A*lambda = -Phi(q). For small islands (<16 bodies) a dense solver is acceptable. For larger islands use sparse factorization with fill-reducing ordering. All allocation from the frame arena.

## Acceptance Criteria

Correct lambda for known test cases (2-body penetration, 10-body stack); no heap allocation during solve; handles degenerate/zero-penetration gracefully

