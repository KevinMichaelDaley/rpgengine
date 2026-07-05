---
id: rpg-rnno
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
# srd-018: srd_eikonal.cpp — differentiable eikonal solver

DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!

Implement the continuous eikonal equation solver (Section 6.1): |∇T| = 1/v with T=0 at anchor points. Uses Godunov upwind finite differences on the coarse SRD grid (128^3 at 1m/voxel). Fast sweeping method with fixed iteration count. Differentiable: backpropagates gradients from T(to) through the sweep iterations to v(x) → occ(x) → SDF parameters.

SymX symbolic differentiation handles the sweep iterations natively. The solver takes the scene occupancy field and anchor positions as input, returns the T field and T(to) value.

RED-phase: tests/procgen/srd/srd_eikonal_tests.cpp — empty grid T(to) = Euclidean distance; wall between source and target increases T; gradient pushes wall out of the way.

## Acceptance Criteria

T field computed correctly on empty grid (matches Euclidean); wall obstacle produces longer path; gradient of T(to) w.r.t. occ has correct sign (removing obstacle reduces T); converges within 10 sweeps on 128^3 grid

