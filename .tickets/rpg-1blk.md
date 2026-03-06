---
id: rpg-1blk
status: open
deps: [rpg-qslo]
links: []
created: 2026-03-06T06:10:42Z
type: task
priority: 2
assignee: KMD
parent: rpg-ywes
tags: [animation, physics, constraints]
---
# Space conversion and constraint evaluation core

## Summary

Implement the constraint evaluation pipeline: space conversions between world/local/pose/bone coordinate frames, influence blending, and the main solver loop that evaluates constraints in stack order. This is the engine that drives all constraint types for both animation bones and physics bodies.

## Motivation

Every constraint operates in a particular coordinate space (world, local, pose, bone). Before evaluating a constraint, the solver must convert the owner and target transforms into the constraint's expected space, evaluate the constraint, then convert the result back. This space conversion + evaluation loop is shared by all 20 constraint types and must work identically for animation bones and physics rigid bodies.

## Deliverables

### 1. Space conversion functions
```c
void constraint_to_world_space(const skeleton_def_t *skel, uint32_t bone_idx, constraint_space_t space, const mat4 *local, mat4 *out_world);
void constraint_from_world_space(const skeleton_def_t *skel, uint32_t bone_idx, constraint_space_t space, const mat4 *world, mat4 *out_local);
```
Must handle all 4 spaces correctly:
- WORLD: identity transform (already in world)
- LOCAL: relative to parent bone
- POSE: relative to rest pose
- BONE: relative to bone's own rest transform

### 2. Influence blending
Blend between unconstrained and constrained transforms using the constraint's influence factor (0.0 = no effect, 1.0 = full effect). Must blend position (lerp), rotation (slerp), and scale (lerp) independently.

### 3. Constraint solver loop (`constraint_solver_t`)
```c
typedef struct constraint_solver {
    uint32_t max_bones;
    uint32_t max_constraints_per_bone;
    mat4 *pose_matrices;      /* current pose (input/output) */
    mat4 *rest_matrices;      /* rest pose (read-only) */
    /* workspace arrays allocated at init */
} constraint_solver_t;
```
- Evaluates constraints per-bone in skeleton order (parents before children)
- Within each bone, evaluates constraints in stack order (array index)
- Calls per-type evaluation functions via dispatch table
- Supports iterative constraints (IK) with configurable iteration count

### 4. Constraint evaluation dispatch
Function pointer table mapping constraint_type_t → evaluation function:
```c
typedef void (*constraint_eval_fn)(const constraint_def_t *def, const constraint_eval_ctx_t *ctx, mat4 *inout_transform);
```
Subtask implementations (IK, tracking, limits, etc.) register their evaluation functions here.

### 5. Physics adapter interface
Abstract interface allowing the solver to operate on either:
- Animation bones (mat4 transforms, skeleton hierarchy)
- Physics bodies (phys_state_t, body pairs)

This ensures the same constraint evaluation code works for both systems.

## File Structure
```
include/ferrum/animation/constraint_solver.h  — solver struct + API (≤2 types)
include/ferrum/animation/constraint_space.h   — space conversion API
src/animation/constraint/constraint_space.c   — space conversion implementations
src/animation/constraint/constraint_solver.c  — solver init/destroy/evaluate
src/animation/constraint/constraint_dispatch.c — dispatch table + registration
```

## Acceptance Criteria
- [ ] All 4 space conversions correct (verified by round-trip tests)
- [ ] Influence blending works for position, rotation, and scale
- [ ] Solver evaluates constraints in correct order (parents → children, stack order within bone)
- [ ] Dispatch table maps all 20 types (initially to no-op stubs)
- [ ] Physics adapter interface defined (not fully implemented — that's a later subtask)
- [ ] Unit tests: space round-trips, influence at 0/0.5/1, evaluation order verification, dispatch registration
- [ ] Solver uses init-allocated pools (no per-frame malloc)
- [ ] ≤2 public types per header, ≤4 non-static functions per source file
- [ ] Clean under -Wall -Wextra -Wpedantic


