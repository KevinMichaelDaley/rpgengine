---
id: phys-100
status: open
deps: [phys-000]
links: []
created: 2026-02-06T05:20:00.000000000-08:00
type: epic
priority: 1
---
# Phase 1: Complete Pipeline (Single-Threaded)

**Goal:** Implement all 14 stages with sphere-only colliders, single-threaded.
Full pipeline structure from the start—no simplified versions.

## Overview

This phase delivers the complete physics tick:
- All 14 pipeline stages implemented
- Sphere-sphere collision only (box/capsule in Phase 2)
- Single-threaded execution (parallelism in Phase 3)
- Full tier classification (initially all T0)
- Spatial grid broadphase
- Manifold caching with warmstart
- Stabilization hints (friction boost, restitution suppression)
- TGS solver with contact + friction constraints
- Island-based solving
- Integration with sleep detection
- Network snapshot encoding/decoding

## Subtasks

- phys-101: Step Plan Stage (Stage 0)
- phys-102: Tier Classification Stage (Stage 1)
- phys-103: Spatial Index Update Stage (Stage 2)
- phys-104: Halo Closure Stage (Stage 3)
- phys-105: AABB Update Stage (Stage 4)
- phys-106: Broadphase Stage (Stage 5)
- phys-107: Sphere-Sphere Narrowphase (Stage 6)
- phys-108: Manifold Build + Cache Merge (Stage 7)
- phys-109: Stabilization Hints Stage (Stage 8)
- phys-110: Constraint Build Stage (Stage 9)
- phys-111: Island Build Stage (Stage 10)
- phys-112: TGS Solve Stage (Stage 11)
- phys-113: Integrate + Sleep Stage (Stage 12)
- phys-114: Cache Commit + Events Stage (Stage 13)
- phys-115: Tick Function (Stages 0-14)
- phys-116: Network Snapshot Encoding
- phys-117: Phase 1 Integration Test + Benchmark

## Key Architectural Principle

All stages follow pure functional transformation:
- Read from input buffers
- Write to distinct output buffers
- No aliasing within a stage

## Performance Targets

- 100 spheres, 30 Hz, 2 substeps: < 1.5 ms/tick
- 1000 spheres, 30 Hz, 2 substeps: < 15 ms/tick (single-threaded)
- Snapshot encode 100 bodies: < 50 µs
