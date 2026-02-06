---
id: phys-300
status: open
deps: [phys-200]
links: []
created: 2026-02-06T11:09:00.000000000-08:00
type: epic
priority: 2
---
# Phase 3: Parallel Jobs


**Goal:** Convert all parallel stages to use job system. Target ~3.5× speedup at 8 threads.

## Subtasks

- phys-301: Physics Job Infrastructure
- phys-302: Parallelize Tier Classification
- phys-303: Parallelize Spatial Update
- phys-304: Parallelize Broadphase
- phys-305: Parallelize Narrowphase
- phys-306: Parallelize Manifold Build
- phys-307: Parallelize Stabilization
- phys-308: Parallelize Constraint Build
- phys-309: Parallelize TGS Solve (T0/T1)
- phys-309b: Parallelize XPBD Solve (T2–T4)
- phys-310: Parallelize Integrate
- phys-311: Phase 3 Integration Test + Benchmark

## Performance Targets

- 1000 bodies, 4 threads: < 1.5 ms/tick
- 3000 bodies, 8 threads: < 2.5 ms/tick
- 5000 bodies, 8 threads: < 3.0 ms/tick

## Key Constraint

TGS and XPBD run concurrently on different thread pools (Stage 11a/11b).
Deterministic results required (bit-exact vs single-threaded).

