---
id: phys-200
status: open
deps: [phys-100]
links: []
created: 2026-02-06T05:20:00.000000000-08:00
type: epic
priority: 2
---
# Phase 2: Box and Capsule Colliders

**Goal:** Complete narrowphase for all 9 primitive collision pairs.

## Overview

This phase adds box and capsule colliders to the existing pipeline:
- Sphere-Sphere ✓ (Phase 1)
- Sphere-Box
- Sphere-Capsule
- Box-Box (SAT with 15 axes)
- Box-Capsule
- Capsule-Capsule

All other pipeline stages remain unchanged—we're just expanding Stage 6 (Narrowphase).

## Subtasks

- phys-201: Sphere-Box Narrowphase
- phys-202: Sphere-Capsule Narrowphase
- phys-203: Box-Box Narrowphase (SAT)
- phys-204: Box-Capsule Narrowphase
- phys-205: Capsule-Capsule Narrowphase
- phys-206: AABB Update for Rotated Shapes
- phys-207: Phase 2 Integration Test + Benchmark

## Key Algorithms

- **Box-Box:** Separating Axis Theorem with 15 axes (3+3+9)
- **Capsule:** Line segment closest point + sphere test

## Performance Targets

- 100 boxes, 30 Hz, 2 substeps: < 2 ms/tick
- 100 capsules, 30 Hz, 2 substeps: < 2 ms/tick
- 100 mixed primitives: < 2 ms/tick

## Network Tests

- Snapshot encode/decode with all primitive types
- Delta compression with mixed types
