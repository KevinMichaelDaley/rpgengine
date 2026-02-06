---
id: phys-300
status: open
deps: [phys-200]
links: []
created: 2026-02-06T05:20:00.000000000-08:00
type: epic
priority: 2
---
# Phase 3: Parallel Jobs

**Goal:** Convert all parallel stages to use job system.

## Overview

This phase adds parallelism to all suitable stages:
- Stage 1: Tier Classification (parallel)
- Stage 2: Spatial Update (parallel)
- Stage 4: AABB Update (parallel)
- Stage 5: Broadphase (parallel by grid cell)
- Stage 6: Narrowphase (parallel by pair)
- Stage 7: Manifold Build (parallel by pair)
- Stage 8: Stabilization (parallel)
- Stage 9: Constraint Build (parallel)
- Stage 11: TGS Solve (parallel by island)
- Stage 12: Integrate (parallel)

## Subtasks

- phys-301: Job Infrastructure (counters, dispatch)
- phys-302: Parallelize Tier Classification
- phys-303: Parallelize Spatial Update
- phys-304: Parallelize Broadphase
- phys-305: Parallelize Narrowphase
- phys-306: Parallelize Manifold Build
- phys-307: Parallelize Constraint Build
- phys-308: Parallelize TGS Solve (per-island)
- phys-309: Parallelize Integrate
- phys-310: Phase 3 Integration Test + Benchmark

## Key Principle

Islands enable zero-contention parallel solving. Each island is independent.

## Performance Targets

- 1000 bodies, 4 threads: < 5 ms/tick
- 5000 bodies, 8 threads: < 10 ms/tick
- Linear scaling up to 4 threads
