---
id: rpg-mtqh
status: open
deps: []
links: []
created: 2026-03-06T09:19:50Z
type: epic
priority: 1
assignee: KMD
---
# Unified animated-body physics pipeline

Replace the current sequential constraint_solver_evaluate with a physics-integrated approach where animated skeleton bones become phys_body_t entries and ALL constraints (animation + physics + contacts) are solved together by the XPBD solver.

## Architecture

### Core loop (per tick, per animated skeleton)

1. FK propagation: local_pose → world bone matrices
2. bone_to_body(): convert each bone mat4 → phys_body_t (pos, orient, inv_mass)
   - Root/control bones: kinematic (inv_mass=0, driven by animation input)
   - Deform bones: dynamic (inv_mass derived from bone mass property)
3. anim_constraint_to_rows(): map each constraint_def_t → phys_constraint_t
   - Copy Location → 3 positional rows (equivalent to ball joint)
   - Copy Rotation → 3 angular rows
   - IK → iterative end-effector position constraint
   - Child Of → ball joint to parent bone body
   - Limits → clamped bilateral rows
4. joint_to_rows(): map fskel joint descriptors → phys_constraint_t (via existing phys_joint_build_*)
5. If per-bone colliders (rpg-5ysw): broadphase/narrowphase → contact constraints
6. XPBD solve: all rows together, multiple iterations
7. Integration: animation target + external forces (Euler-Verlet)
   - Animation pulls bones toward target pose
   - External forces (gravity, contacts, explosions) perturb bones
   - Result: natural blend between animation and physics
8. Write solved body states → bone world matrices
9. Swap double buffer (anim_data_curr ↔ anim_data_next)

### Key design decisions

- Animation advances the interpolation TARGET, not replaces integration
- Standard joints from fskel keep skeleton structurally sound under force
- Animation constraints and physics constraints are the same phys_constraint_t
- Root/control bones are kinematic (inv_mass=0), deform bones are dynamic
- Per-bone collision geo (rpg-5ysw) required for world contact

### New tier: PHYS_TIER_ANIMATED
- Lives in the physics pipeline alongside T0-T5
- Gets XPBD solve (not TGS) 
- Animated bodies form their own islands (one per skeleton)
- Contact constraints from narrowphase are appended to skeleton island

### Dependencies
- rpg-5ysw: per-bone collision geometry (broadphase participation)
- rpg-rcii: joint metadata in fskel (structural integrity)

### Files to create/modify
- src/animation/adapter/bone_to_body.{h,c}: bone mat4 → phys_body_t
- src/animation/adapter/anim_constraint_to_rows.{h,c}: constraint → phys_constraint_t
- src/animation/adapter/anim_body_tier.{h,c}: tier integration into tick pipeline
- src/animation/adapter/anim_integration.{h,c}: target+forces integration
- src/physics/world/tick_parallel.c: add animated body stage
- include/ferrum/physics/phys_types.h: add PHYS_TIER_ANIMATED

## Acceptance Criteria

- Animated skeleton bones appear as phys_body_t in physics solver
- Animation constraints solved by XPBD (order-independent)
- External forces affect animated bodies (ragdoll behavior)
- Double-buffered pose data with atomic swap
- Walk cycle visual test shows correct IK-driven movement
- Adding/removing constraints at runtime works
- Multiple animated skeletons solve independently
- Performance: <0.5ms for 333-bone skeleton at 4 XPBD iterations
- All existing physics tests pass
- All existing animation tests pass

