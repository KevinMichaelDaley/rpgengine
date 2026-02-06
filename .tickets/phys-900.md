---
id: phys-900
status: open
deps: [phys-600]
links: []
created: 2026-02-06T05:20:00.000000000-08:00
type: epic
priority: 3
---
# Phase 9: Mesh Colliders

**Goal:** Triangle mesh collision for static geometry.

## Overview

Add mesh collider support:
- BVH over triangles (per-mesh)
- Primitive-vs-triangle narrowphase
- Only for static bodies (no mesh-mesh)

## Subtasks

- phys-901: Mesh Collider Structure
- phys-902: Mesh BVH Build
- phys-903: Sphere-vs-Triangle
- phys-904: Box-vs-Triangle
- phys-905: Capsule-vs-Triangle
- phys-906: Phase 9 Integration Test

## Performance Targets

- 10k triangle mesh: < 1 ms for single query
- Terrain collision: < 2 ms total
