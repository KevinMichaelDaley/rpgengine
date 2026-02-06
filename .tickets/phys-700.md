---
id: phys-700
status: open
deps: [phys-400]
links: []
created: 2026-02-06T05:20:00.000000000-08:00
type: epic
priority: 3
---
# Phase 7: Advanced Stability

**Goal:** Production-quality stability features.

## Overview

Improve solver stability for complex scenarios:
- Better manifold point reduction (deepest + spread)
- Speculative contacts (prevent tunneling)
- Position-level solve (split impulse)
- Contact graph sleeping

## Subtasks

- phys-701: Manifold Point Reduction Algorithm
- phys-702: Speculative Contacts
- phys-703: Split Impulse Position Solve
- phys-704: Contact Graph Sleeping
- phys-705: Phase 7 Integration Test

## Stability Tests

- 20-high box tower stable for 1000+ frames
- Fast-moving objects don't tunnel through thin walls
- Stacks don't creep or drift
