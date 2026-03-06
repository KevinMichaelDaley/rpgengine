---
id: rpg-qslo
status: open
deps: []
links: []
created: 2026-03-06T06:10:14Z
type: task
priority: 2
assignee: KMD
parent: rpg-ywes
tags: [animation, physics, constraints]
---
# Unified constraint type system

## Summary

Define the core constraint data types as a unified representation shared by BOTH the animation pose solver AND the physics rigid-body engine. A "Limit Rotation" constraint must work identically whether it restricts a bone in an animation skeleton or a rigid-body joint in the physics engine. This is the foundation that all other subtasks build on.

## Motivation

The existing physics engine has 3 joint types (distance, ball, hinge) with a Jacobian-row-based solver. Animation pose constraints need the same categories of restriction (limits, tracking, copying) but operate on bone transforms. Rather than building two parallel systems, we define constraint types ONCE and let both the animation solver and physics solver consume them.

## Deliverables

### 1. Constraint type enum (`constraint_type_t`)
All 20 Blender-compatible constraint types as enum values:
- CONSTRAINT_IK, CONSTRAINT_SPLINE_IK
- CONSTRAINT_CHILD_OF
- CONSTRAINT_COPY_TRANSFORMS, CONSTRAINT_COPY_ROTATION, CONSTRAINT_COPY_LOCATION, CONSTRAINT_COPY_SCALE
- CONSTRAINT_DAMPED_TRACK, CONSTRAINT_TRACK_TO, CONSTRAINT_LOCKED_TRACK
- CONSTRAINT_LIMIT_ROTATION, CONSTRAINT_LIMIT_LOCATION, CONSTRAINT_LIMIT_SCALE
- CONSTRAINT_TRANSFORMATION, CONSTRAINT_ACTION
- CONSTRAINT_CLAMP_TO, CONSTRAINT_FLOOR, CONSTRAINT_MAINTAIN_VOLUME, CONSTRAINT_SHRINKWRAP
- CONSTRAINT_PIVOT

### 2. Per-type parameter structs
Each constraint type gets a dedicated parameter struct with all Blender-compatible fields:
- constraint_ik_params_t: chain_length, target_bone_idx, pole_target_idx, iterations, weight, use_tail, orient_weight
- constraint_limit_rotation_params_t: min_x/y/z, max_x/y/z, use_limit_x/y/z, owner_space
- (etc. for all 20 types)

### 3. Tagged union (`constraint_def_t`)
```c
typedef struct constraint_def {
    constraint_type_t type;
    float influence;           /* 0.0–1.0 blending factor */
    constraint_space_t owner_space;
    constraint_space_t target_space;
    uint32_t target_bone_idx;  /* bone index or UINT32_MAX for external */
    union { ... } params;
} constraint_def_t;
```

### 4. Constraint space enum
CONSTRAINT_SPACE_WORLD, CONSTRAINT_SPACE_LOCAL, CONSTRAINT_SPACE_POSE, CONSTRAINT_SPACE_BONE — needed for both animation and physics evaluation.

### 5. Skeleton definition struct (`skeleton_def_t`)
- Joint names, parent indices, rest transforms (local + world)
- Per-joint constraint list (array of constraint_def_t with count)
- This struct is the bridge between .fskel format and runtime

## File Structure
```
include/ferrum/animation/constraint_types.h   — enum + space enum (≤2 types)
include/ferrum/animation/constraint_params.h  — all param structs (forward-declared, ≤2 public types: constraint_def_t + skeleton_def_t)
src/animation/constraint/constraint_types.c   — constraint_type_name() lookup, validation helpers
```

## Acceptance Criteria
- [ ] All 20 constraint types defined as enum values
- [ ] Each type has a dedicated params struct with all Blender-compatible fields documented
- [ ] Tagged union constraint_def_t with influence, owner/target space, target bone
- [ ] skeleton_def_t holds joint hierarchy + per-joint constraint arrays
- [ ] constraint_space_t enum with WORLD/LOCAL/POSE/BONE values
- [ ] All public APIs documented with Doxygen comments
- [ ] Unit tests: type enum round-trip, constraint_def_t initialization, skeleton_def_t allocation/free
- [ ] ≤2 public types per header, ≤4 non-static functions per source file
- [ ] Clean under -Wall -Wextra -Wpedantic
- [ ] No dependency on physics headers or renderer headers (pure data types)


