---
id: rpg-r82r
status: closed
deps: [rpg-a3dm, rpg-5j3b, rpg-rnno, rpg-icvq]
links: []
created: 2026-07-05T06:48:36Z
type: task
priority: 0
assignee: KMD
parent: rpg-t6ia
tags: [procgen]
---
# srd-016: srd_loss_primitives.cpp — 10 differentiable primitives

DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!

Implement the 10 differentiable loss primitives from Section 5.1: PathDistance, LineOfSight, NonPenetration, MinimumSize, Separation, Containment, AdjacencyCount, HeightSpan, StairAlignment, FloorAccessibility. Each is a SymX ScalarFunction element. Non-PDE primitives (NonPenetration, MinimumSize, Separation, Containment, AdjacencyCount, HeightSpan, StairAlignment) compute energy directly from geometry parameters and occupancy. PDE primitives (PathDistance, LineOfSight, FloorAccessibility) call the eikonal/transport solvers.

RED-phase: tests/procgen/srd/srd_loss_primitives_tests.cpp — for each primitive, verify correct energy value in simple known configurations (e.g., two adjacent rooms have PathDistance ≈ their center-to-center distance).

## Acceptance Criteria

All 10 primitives compile with SymX; each returns correct energy for test cases; energy decreases as geometry approaches satisfying condition

