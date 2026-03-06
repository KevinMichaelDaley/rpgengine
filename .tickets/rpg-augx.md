---
id: rpg-augx
status: in_progress
deps: [rpg-1blk]
links: []
created: 2026-03-06T06:11:42Z
type: task
priority: 2
assignee: KMD
parent: rpg-ywes
tags: [animation, physics, constraints]
---
# Transform copy and tracking constraints

## Summary

Implement the transform copy family (Copy Transforms, Copy Rotation, Copy Location, Copy Scale) and tracking family (Damped Track, Track To, Locked Track). These are single-step, non-iterative constraints that are the most commonly used in both animation rigs and physics setups.

## Motivation

Copy constraints are used everywhere: a weapon bone copying a hand's rotation, a camera tracking an object, a physics body mirroring another's orientation. Tracking constraints point bones at targets — essential for eyes, turrets, procedural look-at, and physics-driven aiming. All of these must work identically whether driving animation bones or physics bodies.

## Deliverables

### 1. Copy Transforms
Copies the full TRS (translation, rotation, scale) from target to owner. mix_mode controls how the copied transform combines with the existing one (REPLACE, BEFORE, AFTER, BEFORE_FULL, AFTER_FULL — matching Blender semantics).

### 2. Copy Rotation
Copies rotation only. Supports per-axis masking (copy only X, Y, and/or Z), inversion per axis, and mix modes. Must handle Euler angle extraction and recombination correctly.

### 3. Copy Location
Copies position only. Supports per-axis masking, inversion per axis, and offset mode (additive vs. replace).

### 4. Copy Scale
Copies scale only. Supports per-axis masking, power mode (raised to exponent), and additive mode.

### 5. Damped Track
Rotates the owner bone to point its track axis (±X, ±Y, ±Z) toward the target using the smallest single-axis rotation. This is the simplest tracking constraint — no up-axis, just minimal rotation.

### 6. Track To
Points the owner's track axis at the target while keeping a specified up axis aligned. Uses Gram-Schmidt orthogonalization to construct the final rotation. More controllable than Damped Track but can gimbal lock.

### 7. Locked Track
Rotates around a specified lock axis to point the track axis as close to the target as possible. The lock axis is guaranteed to remain unchanged. Used for turrets, hinged tracking.

### 8. Physics integration
All copy/track constraints can reference physics body transforms as targets. When the physics engine is the constraint owner, these map to:
- Copy Location → positional constraint (like ball joint target)
- Copy Rotation → angular constraint
- Track To → angular motor with target orientation
- Limit constraints in previous physics joints can be expressed as Copy + Limit combos

## File Structure
```
src/animation/constraint/copy_transforms.c  — Copy Transforms, Copy Rotation
src/animation/constraint/copy_location.c    — Copy Location, Copy Scale
src/animation/constraint/track_damped.c     — Damped Track
src/animation/constraint/track_to.c         — Track To, Locked Track
```

## Acceptance Criteria
- [ ] Copy Transforms replicates exact TRS from target in all mix modes
- [ ] Copy Rotation handles per-axis masking and inversion correctly
- [ ] Copy Location handles per-axis masking, inversion, and offset mode
- [ ] Copy Scale handles per-axis masking and power mode
- [ ] Damped Track produces minimal rotation (verified via angle comparison)
- [ ] Track To maintains up-axis alignment (verified via dot product)
- [ ] Locked Track preserves lock axis exactly (verified via axis comparison)
- [ ] All constraints work with influence blending (0.0, 0.5, 1.0)
- [ ] All constraints handle identity transforms and degenerate cases (target == owner position for tracking)
- [ ] All constraints registered in dispatch table
- [ ] Unit tests for each constraint type: happy path, axis masking, edge cases, influence blending
- [ ] ≤4 non-static functions per source file
- [ ] Clean under -Wall -Wextra -Wpedantic


