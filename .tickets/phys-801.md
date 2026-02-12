---
id: phys-801
status: in_progress
deps: [phys-408]
links: [phys-800]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 8.1: Joint Structures


**Parent Epic:** phys-800 (Phase 8: Joints)

## Files

- `include/ferrum/physics/joint.h`
- `src/physics/constraint/joint_distance.c`
- `src/physics/constraint/joint_hinge.c`
- `src/physics/constraint/joint_ball.c`

## Types

- Distance joint (spring-damper)
- Hinge joint (1 DOF rotation)
- Ball joint (3 DOF rotation)

## Acceptance Criteria

- [ ] All three joint types defined
- [ ] Joint generates constraint rows compatible with existing solver

