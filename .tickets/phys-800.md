---
id: phys-800
status: closed
deps: [phys-400]
links: []
created: 2026-02-06T11:09:00.000000000-08:00
type: epic
priority: 2
---
# Phase 8: Joints


**Goal:** Non-contact constraints (distance, hinge, ball joints) + ragdoll transition.

## Subtasks

- phys-801: Joint Structures
- phys-802: Joint Constraint Build
- phys-803: Joint in Island Build
- phys-804: Phase 8 Integration Test (includes ragdoll)

Ragdoll: compound collider → individual bodies + ball joints at bone pivots.
Requires both compound collider (Phase 0, Step 0.3b) and joints.

