# Physics-Driven Animation Pipeline

## Problem
Visual tests for physics-driven animation (ragdoll drop, anim+force, IK ground,
constraint convergence) use fake manual ragdoll simulation instead of the real
physics world + tick runner pipeline. The skeleton joints need to be solved by
the SAME XPBD solver that handles collision contacts, inside the physics substep
loop, so animation constraints get substep-level stability.

## Architecture (CORRECTED)
**Animation evaluation runs inside `integrate()` for anim-tier bodies**, not in
a pre-tick callback. This way:
1. At the start of each substep, the animation callback sets kinematic bone
   positions (e.g. walk cycle IK targets)
2. The XPBD solver enforces skeleton joints + collision contacts simultaneously
3. Both animation and physics constraints substep together → stability

### Pipeline flow (per substep):
```
┌─ Substep loop ─────────────────────────────────────────────┐
│  Animation callback → set kinematic body positions         │
│  AABB update                                               │
│  Broadphase / narrowphase / island build                   │
│  TGS solve (T0/T1) + XPBD solve (T2-T4, joints+contacts)  │
│  Integrate (positions from solved velocities)               │
│  Buffer swap                                                │
└────────────────────────────────────────────────────────────┘
```

### Key insight
The skeleton joints from fskel are already real XPBD constraints in the world
(via `phys_anim_entity_create()` → `anim_joint_descs_to_joints()` →
`phys_world_add_joint()`). What was missing was running animation evaluation
at the substep level to update kinematic bone targets.

## Implementation

### Core engine changes
- [x] `phys_anim_entity.h/create.c/sync.c` — register skeleton as world bodies
- [x] `phys_tick_runner.h/.c` — pre_tick_cb field (KEEP for per-tick setup)
- [ ] Add `anim_substep_cb` + `anim_substep_user` to `phys_world_t` or
      `phys_world_config_t` — callback invoked at start of each substep in
      `tick_parallel.c`, before broadphase/narrowphase/solver
- [ ] Wire callback in `tick_parallel.c` substep loop (line ~749)
- [ ] Update visual tests to use substep callback

### Visual test rewrites
- [x] `p005_visual_ragdoll_drop.c` — uses physics world + tick runner (pure ragdoll, no animation callback needed)
- [~] `p005_visual_anim_force.c` — needs substep callback instead of pre-tick
- [~] `p005_visual_ik_ground.c` — needs substep callback instead of pre-tick
- [ ] `p005_visual_constraint_converge.c` — needs rewrite with 4 animated entities
- [ ] Run visual tests with video capture
- [ ] Commit and push

## Notes
- The pre-tick callback is still useful for per-tick setup (e.g. reading input),
  but animation evaluation MUST be per-substep
- Kinematic bones: animation sets position each substep. Dynamic bones: solver
  moves them via joint constraints. Non-kinematic bones get gravity + collision.
- For the ragdoll drop test, all bones are dynamic → no animation callback needed
- `phys_anim_entity_push_kinematic()` writes to kinematic body positions in the
  world — call this from the substep callback
