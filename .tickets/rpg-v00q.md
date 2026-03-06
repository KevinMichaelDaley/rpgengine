---
id: rpg-v00q
status: closed
deps: [rpg-s9ob, rpg-augx, rpg-9sjh, rpg-ngt5, rpg-pkgw, rpg-j8n0, rpg-b17x]
links: []
created: 2026-03-06T06:14:45Z
type: task
priority: 2
assignee: KMD
parent: rpg-ywes
tags: [animation, physics, constraints, visual-test]
---
# Visual tests: constraint system demonstration battery

## Summary

Battery of visual tests that demonstrate every constraint type working on both animation bones and physics bodies. Each test renders to video with PPM snapshots for verification. This is the final validation that the unified constraint system works end-to-end.

## Motivation

Constraint behavior is fundamentally visual — correctness is best verified by seeing bones move, track targets, respect limits, and interact with physics. These tests serve as both regression tests and documentation of expected behavior.

## Deliverables

### Test 1: IK Chain Reach (`visual_ik_reach.c`)
A 5-bone arm chain with an IK constraint. Target orbits in a circle. Verifies:
- Chain follows target smoothly
- Chain fully extends when target is at max reach
- Chain relaxes when target moves closer
- Pole target controls elbow bend direction
Duration: 3 seconds, 90 frames, orbit camera

### Test 2: IK vs Physics Ragdoll (`visual_ik_ragdoll.c`)
Same 5-bone arm, first half animated with IK (kinematic), second half switches to ragdoll (dynamic) and falls under gravity. Verifies:
- Smooth transition from animation to physics
- Joint limits respected during ragdoll fall
- No explosion or instability at mode switch
Duration: 4 seconds, 120 frames, side camera

### Test 3: Tracking Constraints (`visual_tracking.c`)
Three bones with Damped Track, Track To, and Locked Track respectively, all targeting the same orbiting point. Verifies:
- Damped Track takes shortest rotation path
- Track To maintains up-axis alignment
- Locked Track only rotates around lock axis
Duration: 3 seconds, 90 frames, front camera

### Test 4: Limit Constraints (`visual_limits.c`)
A bone driven by sinusoidal rotation with Limit Rotation applied. Shows the bone rotating freely within limits and clamping at boundaries. Also a position-limited bone bouncing off bounds. Verifies:
- Rotation clamping at min/max
- Location clamping at bounds
- Limits work in different spaces (world vs local)
Duration: 3 seconds, 90 frames

### Test 5: Copy Constraints (`visual_copy.c`)
Source bone rotates/translates, target bone copies with various mask configurations:
- Full copy (Copy Transforms)
- Rotation-only copy with axis mask
- Location copy with inversion
- Scale copy with power mode
Duration: 3 seconds, 90 frames

### Test 6: Floor Constraint (`visual_floor.c`)
A bone moved downward by animation, Floor constraint prevents it from going below a ground plane. Ground plane rotates to show use_rotation. Verifies:
- Bone stops at floor level
- Floor rotation affects constraint plane
Duration: 3 seconds, 90 frames

### Test 7: Full Humanoid with Constraints (`visual_humanoid_constrained.c`)
Loads humanoid.glb, applies IK to legs (foot planting), Limit Rotation to knees (no backward bend), Track To on head (looks at orbiting target). Renders full skinned mesh with all constraints active simultaneously. Verifies:
- Multiple constraint types working together
- Constraint evaluation order is correct
- Skinned mesh renders correctly with constrained pose
Duration: 5 seconds, 150 frames, orbit camera

### Test 8: Ragdoll Humanoid (`visual_ragdoll_humanoid.c`)
Humanoid starts in bind pose, switches to full ragdoll, falls and collides with ground plane. Verifies:
- All joints have appropriate limits
- Ragdoll looks natural (no hyper-extension)
- Ground collision works with Floor constraints
- Skinned mesh renders during ragdoll
Duration: 5 seconds, 150 frames, side camera

## File Structure
```
tests/visual/p005_visual_ik_reach.c
tests/visual/p005_visual_ik_ragdoll.c
tests/visual/p005_visual_tracking.c
tests/visual/p005_visual_limits.c
tests/visual/p005_visual_copy.c
tests/visual/p005_visual_floor.c
tests/visual/p005_visual_humanoid_constrained.c
tests/visual/p005_visual_ragdoll_humanoid.c
```

## Acceptance Criteria
- [ ] All 8 visual tests compile and run without GL errors
- [ ] All tests produce video output (MP4) and PPM snapshots
- [ ] IK reach test: end-effector within 0.01 units of reachable targets
- [ ] Ragdoll transition test: no frame with NaN or >10x velocity spike
- [ ] Tracking tests: axis alignment verified via dot product in test assertions
- [ ] Limit tests: constrained values never exceed limits ± epsilon
- [ ] Copy tests: copied values match source within float epsilon
- [ ] Floor test: bone y-coordinate never below floor - epsilon
- [ ] Humanoid tests: all skinned meshes render (no missing body parts)
- [ ] Ragdoll humanoid: bodies come to rest (no perpetual motion)
- [ ] All tests run in < 30 seconds each
- [ ] Video files viewable and show expected behavior

## Dependencies
- Depends on ALL other rpg-ywes subtasks (this is the final validation)
- Depends on existing visual test infrastructure (video_capture, PPM snapshot)
- Depends on skinning shader and skeletal mesh system


