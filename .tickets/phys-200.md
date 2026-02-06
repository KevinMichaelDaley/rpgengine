---
id: phys-200
status: open
deps: [phys-100]
links: []
created: 2026-02-06T11:09:00.000000000-08:00
type: epic
priority: 2
---
# Phase 2: Box and Capsule Colliders


**Goal:** Complete narrowphase for all 9 primitive collision pairs + compound collider integration.

## Collision Pairs

- Sphere-Sphere ✓ (Phase 1)
- Sphere-Box (~0.8 µs)
- Sphere-Capsule (~0.5 µs)
- Box-Box (~1.5 µs, SAT with 15 axes)
- Box-Capsule (~1.2 µs)
- Capsule-Capsule (~1.0 µs)

Plus AABB computation for rotated shapes and compound collider integration testing.

## Subtasks

- phys-201: Sphere-Box Narrowphase
- phys-202: Sphere-Capsule Narrowphase
- phys-203: Box-Box Narrowphase (SAT)
- phys-204: Box-Capsule Narrowphase
- phys-205: Capsule-Capsule Narrowphase
- phys-206: AABB for Rotated Shapes
- phys-207: Phase 2 Integration Test + Benchmark

## Performance Targets

- 100 boxes, 30 Hz: < 1.0 ms/tick
- 100 capsules, 30 Hz: < 1.0 ms/tick
- 100 mixed primitives: < 1.0 ms/tick

