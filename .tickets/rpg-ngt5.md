---
id: rpg-ngt5
status: open
deps: [rpg-1blk]
links: []
created: 2026-03-06T06:12:32Z
type: task
priority: 2
assignee: KMD
parent: rpg-ywes
tags: [animation, physics, constraints]
---
# Transform mapping, action, child-of, and pivot constraints

## Summary

Implement the transform remapping family: Transformation (maps one transform channel to another), Action (drives an animation clip from a transform), Child Of (dynamic re-parenting), and Pivot (changes pivot point for transformations). These are the most complex single-step constraints.

## Motivation

These constraints enable advanced rig behaviors:
- Transformation: a slider bone's X-location drives a jaw bone's Z-rotation (driver-like behavior)
- Action: moving a control bone plays a predefined animation (corrective shapes, mechanical rigs)
- Child Of: detachable parenting (pick up / put down objects, weapon swaps)
- Pivot: shift rotation center for off-axis deformations

In physics: Transformation maps to motors (one body's motion drives another), Child Of maps to dynamic constraint creation/destruction, Pivot maps to offset joint anchors.

## Deliverables

### 1. Transformation Constraint
Maps a source object's transform channel to the owner's transform channel:
- from_channel: LOCATION_X/Y/Z, ROTATION_X/Y/Z, SCALE_X/Y/Z
- to_channel: same set
- from_min, from_max: source range
- to_min, to_max: destination range
- Linearly interpolates: owner_channel = lerp(to_min, to_max, inverse_lerp(from_min, from_max, source_channel))
- Supports extrapolation mode (clamp vs extend)

### 2. Action Constraint
Uses a target's transform channel to scrub through an animation action:
- action_clip: reference to animation clip data (index or pointer)
- transform_channel: which channel of the target drives playback
- min/max: target channel range mapped to action time range
- Evaluates the action at computed time, applies result to owner
- Requires animation clip evaluation (rpg-l2jd) to be available

### 3. Child Of Constraint
Dynamic parenting: owner's transform becomes relative to target's transform.
- use_location_x/y/z, use_rotation_x/y/z, use_scale_x/y/z: per-channel enable
- inverse_matrix: cached inverse of target's transform at "set inverse" time
- When active: owner_world = target_world × inverse × owner_local
- In physics: equivalent to creating/destroying a fixed joint dynamically

### 4. Pivot Constraint
Offsets the rotation pivot point:
- offset: vec3 in owner or target space
- rotation_range: only active when rotation exceeds threshold
- When active: translate to pivot, rotate, translate back

## File Structure
```
src/animation/constraint/transform_map.c    — Transformation constraint
src/animation/constraint/action.c           — Action constraint
src/animation/constraint/child_of.c         — Child Of constraint
src/animation/constraint/pivot.c            — Pivot constraint
```

## Acceptance Criteria
- [ ] Transformation correctly maps source channel to dest channel with linear interpolation
- [ ] Transformation handles extrapolation modes (clamp, extend)
- [ ] Action constraint scrubs animation clip at correct time
- [ ] Child Of produces correct world transform with inverse matrix
- [ ] Child Of supports per-channel enable/disable
- [ ] Pivot shifts rotation center correctly (verified by comparing rotated point positions)
- [ ] All constraints work with influence blending
- [ ] All constraints handle degenerate cases (zero range for Transformation, no action for Action)
- [ ] Physics mapping documented: Transformation → motor, Child Of → dynamic joint, Pivot → anchor offset
- [ ] Unit tests: happy path, edge cases, influence, per-channel masking (Child Of)
- [ ] ≤4 non-static functions per source file
- [ ] Clean under -Wall -Wextra -Wpedantic

## Dependencies
- Action constraint depends on rpg-l2jd (animation clip evaluation) — can be stubbed initially


