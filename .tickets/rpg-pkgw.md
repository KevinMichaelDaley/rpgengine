---
id: rpg-pkgw
status: closed
deps: [rpg-1blk]
links: []
created: 2026-03-06T06:13:01Z
type: task
priority: 2
assignee: KMD
parent: rpg-ywes
tags: [animation, physics, constraints]
---
# Surface and volume constraints (Floor, Clamp To, Shrinkwrap, Maintain Volume)

## Summary

Implement environment-aware constraints: Floor (collision plane), Clamp To (curve restriction), Shrinkwrap (surface projection), and Maintain Volume (volume preservation during deformation). These constraints interact with world geometry and are critical for grounding characters and preventing penetration.

## Motivation

- Floor: feet must not penetrate ground, objects must rest on surfaces. In physics, this IS the contact constraint. In animation, it prevents IK targets from going below ground.
- Clamp To: bones follow rails, cables, or paths. In physics, this maps to a slider/prismatic joint along a curve.
- Shrinkwrap: mesh deformation stays on surfaces (clothing, muscle bulge). In physics, keeps bodies on constraint surfaces.
- Maintain Volume: when a bone scales on one axis, other axes compensate to preserve volume. Critical for cartoony squash-and-stretch.

## Deliverables

### 1. Floor Constraint
Prevents the owner from crossing a plane defined by a target's transform:
- target: bone or body whose XZ plane (or XY, YZ) defines the floor
- offset: distance above the floor plane
- use_rotation: if true, floor plane rotates with target
- floor_location: which side is "below" (FLOOR_BELOW_NEGATIVE_Y, etc.)
- Implementation: project owner onto half-space, clamp if below floor

In physics: this IS the contact/penetration constraint, already implemented as contact manifold resolution. The mapping is direct.

### 2. Clamp To Constraint
Restricts the owner's position to lie on a parametric curve:
- target_curve: array of control points defining a Catmull-Rom or Bézier curve
- main_axis: which axis of the curve maps to the bone's position (X, Y, Z)
- cyclic: whether the curve wraps around
- Implementation: find nearest point on curve to owner, snap owner to curve

### 3. Shrinkwrap Constraint
Projects the owner onto the nearest point of a target mesh surface:
- target: mesh reference (triangle soup)
- shrinkwrap_type: NEAREST_SURFACE, PROJECT (along axis), NEAREST_VERTEX
- distance: offset distance from surface (maintain gap)
- Implementation: BVH or spatial query against target mesh triangles

### 4. Maintain Volume
Compensates scaling on non-free axes to preserve volume:
- free_axis: X, Y, or Z (this axis scales freely)
- volume: reference volume (default 1.0)
- Implementation: if free_axis scales by S, other two axes scale by 1/sqrt(S)

## File Structure
```
src/animation/constraint/floor.c             — Floor constraint
src/animation/constraint/clamp_to.c          — Clamp To constraint
src/animation/constraint/shrinkwrap.c        — Shrinkwrap constraint
src/animation/constraint/maintain_volume.c   — Maintain Volume constraint
```

## Acceptance Criteria
- [ ] Floor prevents owner from going below floor plane
- [ ] Floor rotates with target when use_rotation is true
- [ ] Floor offset works correctly (bone hovers at specified distance)
- [ ] Clamp To snaps owner to nearest curve point
- [ ] Clamp To handles cyclic curves (wrap-around)
- [ ] Shrinkwrap projects owner onto nearest mesh surface point
- [ ] Shrinkwrap maintains specified distance offset from surface
- [ ] Maintain Volume preserves volume when free axis scales (sqrt compensation)
- [ ] All constraints work with influence blending
- [ ] Physics mapping documented: Floor → contact, Clamp To → prismatic on curve, Shrinkwrap → surface constraint
- [ ] Unit tests: above/below floor, on/off curve, nearest surface point, volume preservation math
- [ ] ≤4 non-static functions per source file
- [ ] Clean under -Wall -Wextra -Wpedantic


