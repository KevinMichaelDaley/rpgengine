---
id: phys-000
status: open
deps: []
links: []
created: 2026-02-06T05:20:00.000000000-08:00
type: epic
priority: 1
---
# Phase 0: Foundation Data Structures

**Goal:** Define all core data structures needed by the pipeline.
No simulation yet—just the types, pools, and arenas.

## Overview

This phase creates the complete data structure foundation:
- Core math types and rigid body structure
- All three primitive colliders (sphere, box, capsule)
- AABB computation for all primitives
- Body pool with double-buffering
- Frame arena for per-tick allocations
- Tier list structure (T0-T5)
- Spatial hash grid for broadphase
- Contact and manifold structures
- Persistent manifold cache with warmstart storage
- Constraint and Jacobian structures
- Island structure with union-find
- Physics world container

## Subtasks

- phys-001: Core Math Types
- phys-002: Rigid Body Structure
- phys-003: Collider Structures (Sphere, Box, Capsule)
- phys-004: AABB Structure and Computation
- phys-005: Body Pool and Frame Arena
- phys-006: Tier List Structure
- phys-007: Spatial Hash Grid
- phys-008: Contact and Manifold Structures
- phys-009: Manifold Cache (Persistent)
- phys-010: Constraint and Jacobian Structures
- phys-011: Island Structure
- phys-012: Physics World Container
- phys-013: Phase 0 Integration Test

## Key Principle

All data structures support the full 14-stage pipeline from day one.
No simplified versions—this foundation enables the complete architecture.
