---
id: phys-800
status: open
deps: [phys-400]
links: []
created: 2026-02-06T05:20:00.000000000-08:00
type: epic
priority: 3
---
# Phase 8: Joints

**Goal:** Non-contact constraints (joints).

## Overview

Add constraint types for connected bodies:
- Distance joint (spring-damper)
- Hinge joint (1 DOF rotation)
- Ball joint (3 DOF rotation)
- Fixed joint (weld)

Joints plug into existing constraint pipeline (Stage 9).

## Subtasks

- phys-801: Joint Structure and Pool
- phys-802: Distance Joint
- phys-803: Hinge Joint
- phys-804: Ball Joint
- phys-805: Joint in Island Build
- phys-806: Phase 8 Integration Test

## Test Cases

- Pendulum simulation
- Chain of bodies
- Door hinge
- Ragdoll skeleton
