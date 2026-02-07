---
id: rpg-m9nw
status: open
deps: []
links: [phys-703]
created: 2026-02-07T19:44:11Z
type: epic
priority: 1
assignee: KMD
---
# Sparse Per-Island Position Projection (Replace Baumgarte)

Replace Baumgarte velocity-level stabilization with sparse per-island position projection for TGS-tier bodies, as described in ref/sparse_stabilization.tex. Baumgarte adds energy and causes jitter in stacks, especially with uneven masses. The new approach solves A*lambda = -Phi(q) per island using sparse LDL^T/Cholesky, then applies position corrections and synchronizes velocities. Run the box stack demo (rpg-1bul) before and after to validate stacking quality improvement.

## Acceptance Criteria

Position correction does not add energy; stacking quality improved vs Baumgarte; demo box stacks of 20+ boxes remain stable; solver runs within physics budget

