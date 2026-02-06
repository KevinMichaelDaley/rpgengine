---
id: phys-105
status: closed
deps: [phys-103]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.5: AABB Update Stage (Stage 4)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 4: AABB Update for active bodies only. Within a substep,
only active tiers (T0-T4) need AABB updates.

## Files to create

- `include/ferrum/physics/aabb_update.h`
- `src/physics/stages/aabb_update.c`
- `tests/physics/aabb_update_tests.c`

## API

```c
typedef struct phys_aabb_update_args_t {
    const phys_body_t *bodies;
    const phys_collider_t *colliders;
    const phys_sphere_t *spheres;
    const phys_box_t *boxes;
    const phys_capsule_t *capsules;
    phys_aabb_t *aabbs_out;
    const phys_tier_lists_t *tier_lists;
} phys_aabb_update_args_t;

void phys_stage_aabb_update(const phys_aabb_update_args_t *args);
```

## Implementation

Only update AABBs for bodies in active tiers (T0-T4), skipping T5 (sleeping).

## Acceptance Criteria

- [ ] Only active bodies have AABBs updated
- [ ] T5 (sleeping) bodies skipped
- [ ] AABBs match spatial_update results

## Test Cases

```c
// test_aabb_update_skips_sleeping
// Create bodies: 2 active, 1 sleeping
// Verify only 2 AABBs updated (by checking modification)
```
