---
id: rpg-ywes
status: closed
deps: []
links: []
created: 2026-03-06T06:04:28Z
type: feature
priority: 2
assignee: KMD
---
# Custom Skeletal Mesh Format with Pose Constraints

Design and implement a custom skeletal mesh format that supports pose constraints, and a matching set of constraints in the rigid body engine. This enables Blender-compatible bone constraints for IK, tracking, limits, and procedural animation.

## Motivation

Our glTF loader extracts skeleton hierarchy and inverse bind matrices, but glTF does not export Blender pose constraints. We need a custom format that stores the full skeleton hierarchy with constraint metadata, supports runtime constraint evaluation, and enables procedural animation without baked keyframes.

## Deliverables

### 1. Custom Skeletal Mesh Format (.fskel)
- Binary format extending FVMA with skeleton + constraint data
- Skeleton hierarchy: joint names, parent indices, rest transforms
- Per-joint constraint list with typed parameters
- Blender exporter addon or converter tool

### 2. Pose Constraint Types (Blender-compatible)

**IK / Chain Constraints:**
- Inverse Kinematics (IK): chain_length, target, pole_target, iterations, weight
- Spline IK: chain_length, control_points[], twist_mode

**Parenting:**
- Child Of: target, influence, use_translation/rotation/scale flags

**Transform Copy:**
- Copy Transforms: target, mix_mode, influence
- Copy Rotation: target, axes_mask, mix_mode, influence
- Copy Location: target, axes_mask, invert flags, influence
- Copy Scale: target, axes_mask, influence

**Tracking:**
- Damped Track: target, track_axis
- Track To: target, track_axis, up_axis
- Locked Track: target, track_axis, lock_axis

**Limits:**
- Limit Rotation: min/max per axis, use flags, owner_space
- Limit Location: min/max per axis, use flags, owner_space
- Limit Scale: min/max per axis, use flags

**Transform Mapping:**
- Transformation: source, target, from/to space, mapping_type, axis maps
- Action: target, action_clip, transform_channel, min/max

**Surface/Volume:**
- Clamp To: target_curve, main_axis
- Floor: target, offset, use_rotation, floor_location
- Maintain Volume: free_axis, volume
- Shrinkwrap: target, shrinkwrap_type, distance

**Pivot:**
- Pivot: target, offset, rotation_range

### 3. Runtime Constraint Solver
- Constraint evaluation order (respects Blender stack ordering)
- Iterative IK solver (CCD or FABRIK)
- Constraint influence blending (0.0-1.0)
- Owner/target space conversions (local, world, pose, bone)
- Output: target_pose[] array consumed by physics motor system

### 4. Rigid Body Engine Integration — Sequential Pipeline

**CRITICAL DESIGN PRINCIPLE**: Animation and physics are NOT alternatives — they are sequential stages in a per-tick pipeline. ALL bodies (including dynamic ragdoll bodies) go through BOTH the animation constraint solver AND the physics solver every tick:

```
Per-tick pipeline:
  1. Animation clips → raw bone transforms
  2. Constraint solver (IK, tracking, limits) → target_pose[]
  3. target_pose[] → physics joint motor targets
  4. Physics tick (gravity, collisions, joint solving with motors)
  5. Physics body transforms → bone_world[] → GPU skinning
```

The `motor_strength` parameter (0.0–1.0) on each physics joint controls how strongly the body tracks its animation target:
- `1.0` = animation-dominated (walking character, near-kinematic)
- `0.5` = powered ragdoll (character struggling, e.g., wind, impacts)
- `0.0` = pure ragdoll (death, unconscious — no animation motors)
- Per-bone: upper body `1.0` + legs `0.3` = stumbling character

This eliminates the need for binary "kinematic vs dynamic" switching. Instead, motor_strength is a continuous dial that blends between animation authority and physics authority per bone.

## File Structure
include/ferrum/animation/pose_constraint.h, pose_constraint_solver.h
src/animation/constraint/ — ik_solver.c, spline_ik.c, copy_transforms.c, tracking.c, limits.c, transform_map.c, surface.c, child_of.c, pivot.c
src/animation/pose_constraint_solver.c

## Dependencies
- Scene graph (done)
- Skeletal mesh system (done)
- glTF loader (done)
- Animation clip evaluation (rpg-l2jd, pending)

