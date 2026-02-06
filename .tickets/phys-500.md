---
id: phys-500
status: open
deps: [phys-300]
links: []
created: 2026-02-06T05:20:00.000000000-08:00
type: epic
priority: 2
---
# Phase 5: Raycasts and World Queries

**Goal:** Non-pipeline queries for gameplay.

## Overview

Implement world query functions that run outside the main physics tick:
- Raycast (single hit, all hits)
- Shape overlap (sphere, box)
- Closest point query
- Layer mask filtering

Uses spatial grid for broadphase, then precise shape intersection.

## Subtasks

- phys-501: Raycast Implementation
- phys-502: Ray-Sphere/Box/Capsule Tests
- phys-503: Shape Overlap Query
- phys-504: Closest Point Query
- phys-505: Layer Mask System
- phys-506: Phase 5 Integration Test + Benchmark

## Performance Targets

- 1000 raycasts against 1000 bodies: < 5 ms
- Overlap query: < 0.5 ms
