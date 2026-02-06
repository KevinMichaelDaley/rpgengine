---
id: phys-400
status: open
deps: [phys-300]
links: []
created: 2026-02-06T11:09:00.000000000-08:00
type: epic
priority: 2
---
# Phase 4: Tiered Simulation


**Goal:** Full tier system with per-tier parameters + optimization stack.

Includes distance-based tier classification, per-tier solver parameters,
solver transition logic (TGS↔XPBD), occlusion-based tier demotion,
sphere simplification at distance, and amortized T4 ticking.

## Subtasks

- phys-401: Distance-Based Tier Classification
- phys-402: Per-Tier Solver Parameters
- phys-403: Solver Transition Logic (TGS ↔ XPBD)
- phys-404: Per-Tier Stabilization
- phys-405: Amortized Ticking for T4
- phys-406: Occlusion-Based Tier Demotion
- phys-407: Sphere Simplification at Distance
- phys-408: Phase 4 Integration Test + Benchmark

## Optimization Stack (cumulative)

1. Base mitigations: sleep-stabilize, ragdoll LOD, island split
2. Constraint specialization: planar 3×, sphere 6×, planar sleep 15×
3. Occlusion demotion: visibility_set → T3 (42× cheaper per body)
4. Reduced iterations: T3:4, T4:2 + sphere collider simplification
5. Level design: island-breaking gaps, merge static clutter

## Performance Targets

- 5000 bodies tiered: < 3 ms/tick
- 8000 far-field XPBD: < 3 ms/tick
- 500 T4 background props: < 25 µs

