---
id: rpg-j8n0
status: open
deps: [rpg-qslo, rpg-1blk, rpg-9sjh]
links: []
created: 2026-03-06T06:13:39Z
type: task
priority: 2
assignee: KMD
parent: rpg-ywes
tags: [animation, physics, constraints]
---
# Physics engine constraint integration (ragdoll, bidirectional)

## Summary

Integrate the unified constraint system with the existing physics engine. This creates bidirectional mapping: animation constraints drive physics bodies (kinematic mode) and physics simulation drives animation bones (dynamic/ragdoll mode). The goal is a single constraint definition that works in both contexts.

## Motivation

The physics engine currently has 3 joint types (distance, ball, hinge) implemented as Jacobian-row constraints. The animation system has 20 constraint types. This subtask bridges the two:
- Limit Rotation → physics hinge angle limits
- Limit Location → physics distance joint bounds
- IK → physics CCD/FABRIK on body chains
- Copy Transforms → physics fixed joint (weld)
- Track To → physics angular motor target
- Floor → physics contact plane
- Child Of → physics dynamic joint creation/destruction

The existing phys_constraint_t (body_a, body_b, Jacobian rows, lambda) must be extended or wrapped to accept constraint_def_t as input.

## Deliverables

### 1. Constraint-to-physics mapping layer
```c
/* Convert a unified constraint_def_t into physics joint parameters */
bool constraint_to_physics_joint(const constraint_def_t *def, const skeleton_def_t *skel,
                                  phys_joint_type_t *out_type, phys_joint_params_t *out_params);
```
Maps each constraint type to the closest physics joint equivalent:
- CONSTRAINT_LIMIT_ROTATION → PHYS_JOINT_HINGE with angle limits (or new cone joint for 3-axis)
- CONSTRAINT_COPY_LOCATION → PHYS_JOINT_BALL (lock position)
- CONSTRAINT_COPY_TRANSFORMS → PHYS_JOINT_FIXED (new: 6-DOF lock)
- CONSTRAINT_IK → chain of PHYS_JOINT_BALL with limit rotation on each
- CONSTRAINT_FLOOR → half-space contact constraint

### 2. New physics joint types (extending joint.h)
- PHYS_JOINT_FIXED: 6-DOF lock (3 position + 3 rotation rows)
- PHYS_JOINT_CONE: ball joint with cone angle limit (for shoulder/hip)
- PHYS_JOINT_PRISMATIC: 1-DOF slider along axis (for Clamp To)

### 3. Ragdoll builder
```c
/* Create physics bodies + joints from a skeleton with constraints */
void ragdoll_create(const skeleton_def_t *skel, const mat4 *world_pose,
                    phys_world_t *world, ragdoll_t *out_ragdoll);
```
- Creates one capsule/box body per bone (size from bone length)
- Creates joints between parent-child bones
- Applies limit constraints as joint angle limits
- Stores body↔bone index mapping

### 4. Kinematic ↔ Dynamic switching
```c
void ragdoll_set_kinematic(ragdoll_t *ragdoll, bool kinematic);
void ragdoll_set_bone_kinematic(ragdoll_t *ragdoll, uint32_t bone_idx, bool kinematic);
```
- Kinematic: animation drives bodies (write bone transforms → body transforms)
- Dynamic: physics drives bones (read body transforms → bone transforms)
- Partial: some bones kinematic (upper body animated) + some dynamic (legs ragdoll)

### 5. Transform synchronization
Per-tick sync between animation bone transforms and physics body transforms:
- Animation → Physics: after constraint solver, write bone world transforms to kinematic bodies
- Physics → Animation: after physics tick, read body transforms into bone pose matrices

## File Structure
```
include/ferrum/animation/ragdoll.h                    — ragdoll_t struct + API
src/animation/constraint/constraint_to_physics.c      — mapping layer
src/animation/constraint/ragdoll_create.c             — ragdoll builder
src/animation/constraint/ragdoll_sync.c               — transform synchronization
src/physics/constraint/joint_fixed.c                  — fixed joint (6-DOF)
src/physics/constraint/joint_cone.c                   — cone joint (ball + cone limit)
src/physics/constraint/joint_prismatic.c              — prismatic joint (slider)
```

## Acceptance Criteria
- [ ] All mappable constraint types produce correct physics joint parameters
- [ ] New physics joints (fixed, cone, prismatic) integrate with existing TGS/XPBD solvers
- [ ] Ragdoll builder creates correct body-joint hierarchy from skeleton
- [ ] Ragdoll respects limit rotation constraints as physics angle limits
- [ ] Kinematic mode: animation drives body positions exactly
- [ ] Dynamic mode: physics simulation drives bone transforms
- [ ] Partial kinematic/dynamic works (upper body animated, lower ragdoll)
- [ ] Transform sync runs once per tick, no per-bone allocation
- [ ] Existing physics tests still pass (no regression)
- [ ] Unit tests: mapping correctness, ragdoll creation, kinematic switching, sync round-trip
- [ ] ≤4 non-static functions per source file
- [ ] Clean under -Wall -Wextra -Wpedantic

## Dependencies
- Depends on unified constraint types (rpg-qslo)
- Depends on constraint solver core (rpg-1blk)
- Depends on limit constraints (rpg-9sjh)


