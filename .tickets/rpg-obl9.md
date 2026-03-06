---
id: rpg-obl9
status: open
deps: []
links: []
created: 2026-03-06T09:20:57Z
type: task
priority: 2
assignee: KMD
---
# Animated body physics visual test battery

A comprehensive set of visual tests validating the unified animated-body physics pipeline. Each test exercises a specific aspect of the integration between animation constraints and the XPBD physics solver.

## Test 1: Ragdoll drop (p005_visual_ragdoll_drop.c)
- Load humanoid.fskel with per-bone capsule colliders
- Disable all animation constraints (pure ragdoll)
- Enable ball joints at every parent-child pair
- Drop skeleton from height=5 onto ground plane
- Verify: limbs flop naturally, joints hold, no explosion
- Duration: 3 seconds

## Test 2: Animation + external force (p005_visual_anim_force.c)
- Load humanoid.fskel, run walk cycle animation
- At t=1.5s, apply large lateral impulse to torso bone
- Verify: skeleton stumbles but recovers (animation pulls back)
- Verify: joints don't break (default unbreakable)
- Duration: 4 seconds

## Test 3: IK with physics ground contact (p005_visual_ik_ground.c)
- Load humanoid.fskel with foot capsule colliders
- Place on uneven terrain (3 boxes at different heights)
- Drive foot IK targets to step onto each box
- Verify: feet plant on surfaces via narrowphase contact
- Verify: no foot penetration through boxes
- Duration: 3 seconds

## Test 4: Joint breaking (p005_visual_joint_break.c)
- Load humanoid.fskel with ball joints, break_strength=50N on arm joints
- Apply increasing force to hand bone
- Verify: arm detaches at shoulder when threshold exceeded
- Verify: rest of skeleton remains intact
- Duration: 3 seconds
- Depends on: rpg-x7fe (joint properties)

## Test 5: Constraint convergence (p005_visual_constraint_converge.c)
- Load humanoid.fskel with all 67 forward-reference constraints
- Run XPBD with 1, 2, 4, 8 iterations
- Render 4 skeletons side by side
- Verify: visual convergence improvement with more iterations
- Verify: 4 iterations is sufficient for stable pose
- Duration: 2 seconds

## Test 6: Multi-skeleton interaction (p005_visual_multi_skel.c)
- Load 3 humanoid skeletons at different positions
- All running walk cycle toward each other
- Verify: per-bone colliders prevent interpenetration
- Verify: each skeleton has independent constraint solve
- Duration: 4 seconds

## Shared infrastructure
- All tests use p005_visual_common.h for rendering setup
- Each test captures video to logs/ 
- Each test exits 0 on success
- All tests added to Makefile visual test targets

## Dependencies
- rpg-mtqh: unified animated-body physics pipeline
- rpg-5ysw: per-bone collision geometry (for tests 1,3,6)
- rpg-x7fe: joint properties (for test 4 only)
- rpg-sqf8: walk cycle test (foundation for these)

## Acceptance Criteria

- All 6 visual tests compile and run
- All 6 visual tests exit 0
- All 6 video captures show expected behavior:
  1. Ragdoll flops naturally on ground
  2. Walk cycle recovers from impulse
  3. Feet contact uneven terrain without penetration
  4. Arm detaches at correct force threshold
  5. Visual convergence difference between 1 and 4 iterations
  6. Multiple skeletons walk without intersecting
- Tests run in <10 seconds each
- No memory leaks (valgrind clean)

