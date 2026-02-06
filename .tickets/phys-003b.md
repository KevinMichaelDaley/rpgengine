---
id: phys-003b
status: open
deps: [phys-003, phys-004]
links: [phys-000]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 0.3b: Compound Collider (Animated Hierarchy)


**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

A compound collider is a tree of primitive child colliders (sphere, box, capsule).
Each child has a local transform relative to the parent body. When attached to an
animated entity, the skeleton drives child transforms each frame (kinematic). On
entity death, the compound converts to individual rigid bodies (ragdoll — Phase 8).

## Files to create

- `include/ferrum/physics/compound_collider.h`
- `src/physics/collider/compound_collider.c`
- `tests/physics/compound_collider_tests.c`

## Structures

```c
typedef struct phys_compound_child_t {
    phys_collider_t collider;        // child primitive (sphere/box/capsule)
    uint16_t bone_index;             // skeleton bone driving this child (0xFFFF = static)
    uint16_t flags;                  // KINEMATIC, DETACHABLE, etc.
} phys_compound_child_t;

typedef struct phys_compound_collider_t {
    phys_compound_child_t *children;
    uint16_t child_count;
    uint16_t max_children;
    phys_aabb_t cached_aabb;         // union of all child AABBs (recomputed per frame)
} phys_compound_collider_t;
```

## API

```c
void phys_compound_init(phys_compound_collider_t *cc, phys_compound_child_t *storage, uint16_t max);
void phys_compound_add_child(phys_compound_collider_t *cc, const phys_collider_t *child, uint16_t bone);
void phys_compound_update_transforms(phys_compound_collider_t *cc,
                                     const phys_mat3_t *bone_transforms,
                                     const phys_vec3_t *bone_positions,
                                     uint16_t bone_count);
void phys_compound_compute_aabb(phys_compound_collider_t *cc, phys_aabb_t *out);
```

Narrowphase tests each child independently (broadphase uses the compound AABB
to cull, then per-child tests for actual contacts).

## Test Cases

```c
// test_compound_init_and_add
// test_compound_bone_update_moves_children
// test_compound_aabb_encloses_all_children
// test_compound_separated_child_no_false_positive
// test_compound_narrowphase_per_child
```

## Acceptance Criteria

- [ ] Compound stores tree of primitive children
- [ ] Bone-driven transform update works correctly
- [ ] Compound AABB is tight union of child AABBs
- [ ] Narrowphase dispatches per-child, not per-compound

