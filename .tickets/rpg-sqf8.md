---
id: rpg-sqf8
status: closed
deps: []
links: []
created: 2026-03-06T09:20:10Z
type: task
priority: 2
assignee: KMD
---
# Walk cycle visual test validation

Fix and validate the walk cycle visual test (p005_visual_walk_cycle.c) using the unified animated-body physics pipeline (rpg-mtqh). This test loads humanoid.fskel, animates foot IK targets and trajectory control bone to simulate a simple walk cycle, and renders it.

## What needs to happen

1. Replace current constraint_solver_evaluate() call with the unified pipeline:
   - bone_to_body() to create phys_body_t array from skeleton
   - anim_constraint_to_rows() to map IK/CopyRotation/CopyLocation/ChildOf constraints
   - XPBD solve with 4-8 iterations
   - Write back to bone matrices

2. Animation inputs (same as current):
   - c_traj: forward translation (walk forward)
   - c_foot_ik.l/r: figure-8 foot trajectory with ground contact
   - c_root_master.x: hip bob for natural gait

3. Ground contact:
   - If per-bone colliders available: actual physics ground plane contact
   - Fallback: floor limit constraint on foot bones

4. Verification:
   - Visual: rig visibly moves in captured video (not just control bones)
   - IK chains resolve: leg_ik_nostr.l/r pull leg deform bones
   - Hips bob, feet lift and plant, skeleton translates forward
   - Constraints are order-independent (XPBD, not sequential evaluation)

## Key bones
- c_traj (1): root trajectory, drives forward motion
- c_foot_ik.l (248): left foot IK target
- c_foot_ik.r (229): right foot IK target  
- c_root_master.x: hip bob
- leg_ik_nostr.l/r: IK constraints targeting foot_ik_target.l/r

## Dependencies
- rpg-mtqh: unified animated-body physics pipeline must be implemented first
- rpg-5ysw: per-bone collision geometry for ground contact (optional, can use floor limit)

## Acceptance Criteria

- Walk cycle visual test renders animated humanoid skeleton
- Feet lift off ground and plant cyclically
- Hips bob naturally
- Skeleton translates forward over time
- IK solver moves leg deform bones (not just control bones)
- Constraints solved in <0.5ms per frame (XPBD)
- Video capture shows clear walking animation
- Test passes in CI (exits 0)

