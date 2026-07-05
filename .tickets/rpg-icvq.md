---
id: rpg-icvq
status: open
deps: [rpg-a3dm, rpg-giey]
links: []
created: 2026-07-05T06:48:36Z
type: task
priority: 0
assignee: KMD
parent: rpg-t6ia
tags: [procgen]
---
# srd-019: srd_transport.cpp — differentiable anisotropic transport

DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!

Implement the anisotropic gradient transport solver (Section 6.2): ∇·(a(x)∇R) = 0 with R=1 on source surface, R=0 on boundary. The diffusion tensor a(x) aligns with line-of-sight direction in empty space and blocks perpendicular in solid. Solved with conjugate gradient (10-20 iterations on coarse grid).

Differentiable via adjoint method or SymX's symbolic differentiation through CG iterations. Takes scene occupancy, source room, target direction as input; returns R field and R(target) value.

RED-phase: tests/procgen/srd/srd_transport_tests.cpp — empty space R(target) ≈ 1; wall occlusion R(target) < 0.5; gradient of R(target) w.r.t. wall occupancy is negative (removing wall increases R).

## Acceptance Criteria

R(target) > 0.95 in clear line-of-sight; R(target) < 0.3 through solid wall; gradient sign correct for clearing obstruction; CG converges within 20 iterations

