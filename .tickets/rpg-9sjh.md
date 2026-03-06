---
id: rpg-9sjh
status: open
deps: [rpg-1blk]
links: []
created: 2026-03-06T06:12:05Z
type: task
priority: 2
assignee: KMD
parent: rpg-ywes
tags: [animation, physics, constraints]
---
# Limit constraints (rotation, location, scale)

## Summary

Implement Limit Rotation, Limit Location, and Limit Scale constraints. These clamp transform components to specified ranges and are the most critical constraints for physics integration — they map directly to joint angle limits, position bounds, and scale restrictions.

## Motivation

Limit constraints are the primary bridge between animation and physics. A hinge joint's angle limits in the physics engine are mathematically identical to a Limit Rotation constraint on a bone. By implementing them in the unified constraint system, we get:
- Animation: bones cannot exceed specified ranges during IK or procedural animation
- Physics: joint limits for ragdoll (elbow can't bend backward, knee has limited range)
- Both: same constraint definition drives both systems

## Deliverables

### 1. Limit Rotation
Clamps rotation to min/max Euler angles per axis. Each axis has an independent enable flag. Supports owner_space (apply limits in world, local, or pose space).

Key challenge: Euler angle decomposition order matters. Must match Blender's XYZ Euler convention. Also must handle angle wrapping correctly (e.g., clamping to [-90°, 90°] when current angle is 350°).

### 2. Limit Location
Clamps position to min/max bounds per axis. Each axis has independent min/max enable flags (can limit only max_x without limiting min_x). Supports owner_space.

### 3. Limit Scale
Clamps scale to min/max bounds per axis. Each axis has independent min/max enable flags.

### 4. Physics engine mapping
Direct bidirectional mapping to existing physics joint limits:
- Limit Rotation → hinge joint angle limits (already have lambda_min/lambda_max on angular rows)
- Limit Location → distance joint min/max distance
- Limit Rotation with 3 axes → cone limit for ball joints (new physics constraint type)

This subtask defines the mapping interface; the actual physics constraint types are extended in the physics integration subtask.

## File Structure
```
src/animation/constraint/limit_rotation.c  — Limit Rotation implementation
src/animation/constraint/limit_location.c  — Limit Location implementation
src/animation/constraint/limit_scale.c     — Limit Scale implementation
```

## Acceptance Criteria
- [ ] Limit Rotation clamps correctly for each axis independently
- [ ] Limit Rotation handles angle wrapping (359° clamped to [0°, 180°] → 0°)
- [ ] Limit Rotation respects owner_space (world vs local limits)
- [ ] Limit Location clamps to bounds with independent min/max enable flags
- [ ] Limit Scale clamps to bounds with independent min/max enable flags
- [ ] All limits work with influence blending
- [ ] Limits produce no change when transform is within range
- [ ] Limits handle degenerate ranges (min > max → no clamping? or swap?)
- [ ] Mapping to physics joint limits documented in header comments
- [ ] Unit tests: in-range (no-op), at-boundary, beyond-boundary, per-axis enable, angle wrapping, influence
- [ ] ≤4 non-static functions per source file
- [ ] Clean under -Wall -Wextra -Wpedantic


