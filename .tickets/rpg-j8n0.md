---
id: rpg-j8n0
status: open
deps: [rpg-qslo, rpg-1blk, rpg-9sjh]
links: []
created: 2026-03-06T06:13:39Z
type: task
priority: 2
assignee: KMD
parent: rpg-ywes
tags: [animation, physics, constraints]
---
# Physics engine constraint integration (sequential animation→physics pipeline)

## Summary

Integrate the unified constraint system with the existing physics engine using a **sequential pipeline** model. Every tick, ALL bodies (including dynamic ragdoll bodies) go through both the animation constraint solver AND the physics solver in sequence:

1. **Animation tick**: evaluate clips → apply pose constraints (IK, tracking, limits, copies) → produce **target poses**
2. **Physics tick**: target poses become **motor targets** on physics joints → physics simulation (gravity, collisions, joint solving) → produce **physically plausible final transforms**
3. **Sync back**: physics results → bone transforms → GPU skinning

There is NO binary "kinematic vs dynamic" split. Dynamic bodies are **still driven by animation and IK** — the animation solver produces the desired pose, and the physics solver tries to achieve it while respecting gravity, collisions, and joint limits. The `motor_strength` parameter on each joint controls how strongly physics tries to match the animation target (0.0 = pure ragdoll, 1.0 = near-kinematic, values in between = powered ragdoll).

## Motivation

The physics engine currently has 3 joint types (distance, ball, hinge) implemented as Jacobian-row constraints. The animation system has 20 constraint types. This subtask bridges the two so they work as a unified pipeline:

**The key insight**: a ragdoll character reaching for an object uses IK to compute the arm target, then physics enforces that the arm obeys gravity, doesn't penetrate walls, and respects elbow limits. The animation solver says "arm should be HERE" and the physics solver says "closest physically valid pose is HERE." Both run every tick.

Constraint mappings:
- Limit Rotation → physics hinge/cone angle limits
- IK target → physics motor target on joint chain
- Copy Transforms → physics fixed joint (weld) with motor
- Track To → angular motor target orientation
- Floor → contact plane constraint
- All animation constraints → motor targets for corresponding physics joints

## Per-Tick Pipeline (CRITICAL)

```
┌──────────────────────────────────────────────────────────────────┐
│ Frame N                                                          │
│                                                                  │
│  1. Animation Clip Evaluation                                    │
│     └─ Sample keyframes → raw bone local transforms              │
│                                                                  │
│  2. Pose Constraint Solver (constraint_solver_evaluate)          │
│     └─ IK, tracking, limits, copies → target_pose[bone_count]   │
│                                                                  │
│  3. Target Pose → Physics Motor Targets                          │
│     └─ For each bone/body pair:                                  │
│        joint.motor_target = target_pose[bone_idx]                │
│        joint.motor_strength = per_bone_strength[bone_idx]        │
│                                                                  │
│  4. Physics Tick (broadphase → narrowphase → solve)              │
│     └─ Motors try to reach targets, gravity/collisions resist    │
│     └─ TGS/XPBD solver produces final_state[body_count]         │
│                                                                  │
│  5. Physics → Bone Sync                                          │
│     └─ For each body/bone pair:                                  │
│        bone_world[bone_idx] = body_transform[body_idx]           │
│                                                                  │
│  6. GPU Skinning                                                 │
│     └─ bone_world[] → bone_palette SSBO → vertex shader          │
└──────────────────────────────────────────────────────────────────┘
```

**Motor strength examples:**
- `1.0` = animation-dominated (kinematic-like, e.g., walking character)
- `0.5` = powered ragdoll (e.g., character struggling against force)
- `0.0` = pure ragdoll (e.g., death, unconscious — physics only)
- Per-bone: upper body `1.0` (animated) + legs `0.3` (soft physics) = stumbling character

## Deliverables

### 1. Constraint-to-physics mapping layer
```c
/* Convert a unified constraint_def_t into physics joint parameters */
bool constraint_to_physics_joint(const constraint_def_t *def, const skeleton_def_t *skel,
                                  phys_joint_type_t *out_type, phys_joint_params_t *out_params);
```
Maps each constraint type to the closest physics joint equivalent:
- CONSTRAINT_LIMIT_ROTATION → PHYS_JOINT_HINGE with angle limits (or new cone joint for 3-axis)
- CONSTRAINT_COPY_LOCATION → PHYS_JOINT_BALL (lock position)
- CONSTRAINT_COPY_TRANSFORMS → PHYS_JOINT_FIXED (new: 6-DOF lock)
- CONSTRAINT_IK → chain of PHYS_JOINT_BALL with motor targets from IK solution
- CONSTRAINT_FLOOR → half-space contact constraint

### 2. Motor target system on physics joints
Extend existing joint types with motor target + strength:
```c
typedef struct phys_joint_motor {
    mat4 target_transform;   /* desired pose from animation solver */
    float strength;          /* 0.0 = passive, 1.0 = near-kinematic */
    float max_force;         /* torque/force limit to prevent instability */
} phys_joint_motor_t;
```
Each joint (ball, hinge, fixed, cone) can optionally have a motor that applies forces toward the animation-solved target. Motor forces are added as bias terms in the Jacobian rows.

### 3. New physics joint types (extending joint.h)
- PHYS_JOINT_FIXED: 6-DOF lock (3 position + 3 rotation rows)
- PHYS_JOINT_CONE: ball joint with cone angle limit (for shoulder/hip)
- PHYS_JOINT_PRISMATIC: 1-DOF slider along axis (for Clamp To)

### 4. Ragdoll builder
```c
/* Create physics bodies + joints from a skeleton with constraints */
void ragdoll_create(const skeleton_def_t *skel, const mat4 *world_pose,
                    phys_world_t *world, ragdoll_t *out_ragdoll);
```
- Creates one capsule/box body per bone (size from bone length)
- Creates joints between parent-child bones with motors
- Applies limit constraints as joint angle limits
- Sets initial motor_strength to 1.0 (fully animation-driven)
- Stores body↔bone index mapping

### 5. Per-bone motor strength control
```c
void ragdoll_set_motor_strength(ragdoll_t *ragdoll, float strength);
void ragdoll_set_bone_motor_strength(ragdoll_t *ragdoll, uint32_t bone_idx, float strength);
```
- `1.0` = animation-dominated (motors overpower gravity/collisions)
- `0.0` = pure ragdoll (no motors, physics only)
- Intermediate = powered ragdoll (animation suggests, physics enforces plausibility)
- Per-bone control for partial ragdoll (e.g., limp arm on otherwise animated character)

### 6. Sequential pipeline orchestrator
```c
/* Run the full animation→physics→sync pipeline for one tick */
void ragdoll_tick(ragdoll_t *ragdoll, const constraint_solver_t *solver,
                  const mat4 *anim_pose, float dt);
```
1. Runs constraint solver on anim_pose → target_pose
2. Writes target_pose to joint motors
3. Physics tick runs (externally, not owned by ragdoll)
4. Reads physics body transforms → bone_world output

### 7. Transform synchronization
Per-tick sync that always runs (not conditional on kinematic/dynamic):
- **Pre-physics**: animation solver output → joint motor targets (always)
- **Post-physics**: body transforms → bone world matrices (always)
- Both directions run every tick for every body

## File Structure
```
include/ferrum/animation/ragdoll.h                    — ragdoll_t struct + API
include/ferrum/physics/joint_motor.h                  — phys_joint_motor_t
src/animation/constraint/constraint_to_physics.c      — mapping layer
src/animation/constraint/ragdoll_create.c             — ragdoll builder
src/animation/constraint/ragdoll_tick.c               — sequential pipeline orchestrator
src/animation/constraint/ragdoll_sync.c               — transform synchronization
src/physics/constraint/joint_motor.c                  — motor force computation
src/physics/constraint/joint_fixed.c                  — fixed joint (6-DOF)
src/physics/constraint/joint_cone.c                   — cone joint (ball + cone limit)
src/physics/constraint/joint_prismatic.c              — prismatic joint (slider)
```

## Acceptance Criteria
- [ ] All mappable constraint types produce correct physics joint parameters
- [ ] New physics joints (fixed, cone, prismatic) integrate with existing TGS/XPBD solvers
- [ ] Motor system applies forces toward animation target proportional to motor_strength
- [ ] motor_strength=1.0 produces near-kinematic tracking of animation pose
- [ ] motor_strength=0.0 produces pure ragdoll (no motor forces)
- [ ] motor_strength=0.5 produces powered ragdoll (fights gravity partially)
- [ ] Per-bone motor strength works (some bones animated, some ragdoll)
- [ ] Sequential pipeline runs BOTH animation solver AND physics every tick for dynamic bodies
- [ ] IK solution feeds into physics motor targets (IK arm reaching → physics-plausible arm)
- [ ] Ragdoll builder creates correct body-joint hierarchy from skeleton
- [ ] Ragdoll respects limit rotation constraints as physics angle limits
- [ ] Transform sync runs once per tick, no per-bone allocation
- [ ] Existing physics tests still pass (no regression)
- [ ] Unit tests: motor strength sweep, pipeline ordering, mapping correctness, ragdoll creation
- [ ] ≤4 non-static functions per source file
- [ ] Clean under -Wall -Wextra -Wpedantic

## Dependencies
- Depends on unified constraint types (rpg-qslo)
- Depends on constraint solver core (rpg-1blk)
- Depends on limit constraints (rpg-9sjh)


