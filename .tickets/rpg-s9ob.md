---
id: rpg-s9ob
status: in_progress
deps: [rpg-1blk]
links: []
created: 2026-03-06T06:11:12Z
type: task
priority: 2
assignee: KMD
parent: rpg-ywes
tags: [animation, physics, constraints, ik]
---
# IK solvers (CCD and FABRIK)

## Summary

Implement the two core IK solvers — Cyclic Coordinate Descent (CCD) and Forward And Backward Reaching Inverse Kinematics (FABRIK) — plus Spline IK. These are the most complex constraint types and require iterative solving. They must work for both animation bone chains and physics body chains (e.g., ragdoll limb positioning).

## Motivation

IK is the foundation of procedural animation. A character reaching for an object, feet planting on terrain, or a ragdoll limb being pulled by a constraint all use the same underlying IK math. By implementing IK in the unified constraint system, both the animation solver and physics engine can use it.

## Deliverables

### 1. CCD IK Solver
Cyclic Coordinate Descent — iterates from tip to root, rotating each bone to minimize distance to target. Simple, robust, good for long chains.

Parameters: chain_length, target position/rotation, pole_target (for preferred bend plane), max_iterations, tolerance, weight per bone.

Algorithm:
1. For each iteration:
2.   Walk from tip bone to root of chain
3.   For each bone: rotate to aim end-effector at target
4.   Apply pole target constraint (project onto bend plane)
5. Check convergence (distance < tolerance)

### 2. FABRIK IK Solver
Forward And Backward Reaching — alternates forward pass (root→tip, enforcing bone lengths) and backward pass (tip→root, pulling toward target). Better convergence for position-only targets, preserves bone lengths exactly.

Algorithm:
1. Forward pass: move tip to target, walk toward root maintaining distances
2. Backward pass: reset root position, walk toward tip maintaining distances
3. Repeat until convergence or max iterations
4. Reconstruct rotations from final positions

### 3. Spline IK
Aligns a bone chain along a cubic Bézier or Catmull-Rom spline. Each bone's position is sampled along the curve at uniform arc-length intervals.

Parameters: chain_length, control_points[] (4+ points), twist_mode (tangent, z-up, fixed), chain_offset.

### 4. Integration with existing physics joints
The IK solver must be able to target physics body positions. When a physics body is the IK target, the solver reads the body's world transform. When IK drives physics bodies (kinematic), it writes back the solved transforms.

## File Structure
```
include/ferrum/animation/ik_solver.h       — IK solver API (constraint_ik_solve, constraint_spline_ik_solve)
src/animation/constraint/ik_ccd.c          — CCD implementation
src/animation/constraint/ik_fabrik.c       — FABRIK implementation
src/animation/constraint/ik_spline.c       — Spline IK implementation
src/animation/constraint/ik_common.c       — Shared helpers (pole target, convergence check)
```

## Acceptance Criteria
- [ ] CCD solver converges for 2-bone, 5-bone, and 20-bone chains within tolerance
- [ ] FABRIK solver preserves bone lengths exactly (within float epsilon)
- [ ] Both solvers respect pole target (bend plane constraint)
- [ ] Spline IK distributes bones along curve at equal arc-length intervals
- [ ] IK works with influence blending (partial IK at influence < 1.0)
- [ ] IK handles unreachable targets gracefully (fully extended chain, no NaN)
- [ ] IK handles zero-length chains and degenerate cases (target at bone root)
- [ ] Performance: 100-bone chain solves in < 100µs
- [ ] All solvers registered in constraint dispatch table
- [ ] Unit tests: convergence, pole target, unreachable target, chain length preservation, edge cases
- [ ] ≤4 non-static functions per source file
- [ ] Clean under -Wall -Wextra -Wpedantic


