---
id: phys-104
status: closed
deps: [phys-102, phys-103]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.4: Halo Closure Stage (Stage 3)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 3: Halo Closure. For T0 bodies, expand AABB by velocity and
query grid to promote nearby bodies to T1. This ensures fast-moving objects
don't miss collisions.

## Files to create

- `include/ferrum/physics/halo_closure.h`
- `src/physics/stages/halo_closure.c`
- `tests/physics/halo_closure_tests.c`

## API

```c
typedef struct phys_halo_closure_args_t {
    const phys_body_t *bodies;
    const phys_aabb_t *aabbs;
    const phys_spatial_grid_t *grid;
    phys_tier_lists_t *tier_lists;  // modified in place
    float velocity_margin;          // extra padding
    float dt;
} phys_halo_closure_args_t;

void phys_stage_halo_closure(const phys_halo_closure_args_t *args);
```

## Implementation

```c
void phys_stage_halo_closure(const phys_halo_closure_args_t *args) {
    phys_tier_list_t *t0 = &args->tier_lists->tiers[PHYS_TIER_0_DIRECT];
    phys_tier_list_t *t1 = &args->tier_lists->tiers[PHYS_TIER_1_NEAR];
    
    // For each T0 body, find potential contacts
    for (uint32_t i = 0; i < t0->count; ++i) {
        uint32_t body_idx = t0->indices[i];
        const phys_body_t *body = &args->bodies[body_idx];
        
        // Expand AABB by velocity * dt + margin
        phys_aabb_t swept = args->aabbs[body_idx];
        phys_vec3_t motion = vec3_scale(body->linear_vel, args->dt);
        
        if (motion.x > 0) swept.max.x += motion.x; else swept.min.x += motion.x;
        if (motion.y > 0) swept.max.y += motion.y; else swept.min.y += motion.y;
        if (motion.z > 0) swept.max.z += motion.z; else swept.min.z += motion.z;
        
        phys_aabb_expand(&swept, args->velocity_margin);
        
        // Query grid for neighbors
        uint32_t neighbors[256];
        uint32_t count = phys_spatial_grid_query(args->grid, &swept, neighbors, 256);
        
        // Promote neighbors to T1 if not already in T0
        for (uint32_t j = 0; j < count; ++j) {
            uint32_t neighbor = neighbors[j];
            if (neighbor != body_idx) {
                // TODO: Check if already in T0/T1, add if not
                // For Phase 1 simplicity, all dynamic already in T0
            }
        }
    }
}
```

## Note for Phase 1

In Phase 1, all dynamic bodies start in T0, so halo closure is mostly a no-op.
The structure is here for Phase 4 (tiered simulation).

## Acceptance Criteria

- [ ] Swept AABB computed from velocity
- [ ] Grid queried for neighbors
- [ ] Structure ready for tiered promotion (Phase 4)

## Test Cases

```c
// test_halo_swept_aabb
phys_body_t body;
phys_body_init(&body);
body.position = (phys_vec3_t){0, 0, 0};
body.linear_vel = (phys_vec3_t){100, 0, 0};  // fast-moving

phys_aabb_t aabb = {{-1, -1, -1}, {1, 1, 1}};

// After 1/30s, moves 3.33m, swept AABB should extend to x=4.33
// (Implementation verified manually)

// test_halo_closure_phase1_noop
// All bodies in T0, halo closure doesn't change anything
```
