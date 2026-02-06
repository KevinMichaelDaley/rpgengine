---
id: phys-115
status: open
deps: [phys-101, phys-102, phys-103, phys-104, phys-105, phys-106, phys-107, phys-108, phys-109, phys-110, phys-111, phys-112, phys-113, phys-114]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.15: Tick Function (Stages 0-14)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement the complete tick function that orchestrates all 14 stages
with substep loop and buffer swap.

## Files to create

- `include/ferrum/physics/tick.h`
- `src/physics/world/tick.c`
- `tests/physics/tick_tests.c`

## API

```c
void phys_world_tick(phys_world_t *world, const phys_game_state_t *game);
```

## Implementation

```c
void phys_world_tick(phys_world_t *world, const phys_game_state_t *game) {
    // Reset frame arena
    phys_frame_arena_reset(&world->frame_arena);
    
    // Stage 0: Step Plan
    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, world, game);
    
    // Stage 1: Tier Classification
    phys_tier_lists_t tier_lists;
    phys_stage_tier_classify(&(phys_tier_classify_args_t){...}, &tier_lists);
    
    // Stage 2: Spatial Index Update
    phys_spatial_grid_t grid;
    phys_stage_spatial_update(&(phys_spatial_update_args_t){...});
    
    // Substep loop
    for (uint32_t substep = 0; substep < plan.substeps; ++substep) {
        // Stage 3: Halo Closure
        phys_stage_halo_closure(...);
        
        // Stage 4: AABB Update
        phys_stage_aabb_update(...);
        
        // Stage 5: Broadphase
        phys_collision_pair_t *pairs = phys_frame_arena_alloc(...);
        uint32_t pair_count;
        phys_stage_broadphase(..., &pair_count);
        
        // Stage 6: Narrowphase
        phys_contact_candidate_t *candidates = phys_frame_arena_alloc(...);
        uint32_t candidate_count;
        phys_stage_narrowphase(..., &candidate_count);
        
        // Stage 7: Manifold Build
        phys_manifold_t *manifolds = phys_frame_arena_alloc(...);
        uint32_t manifold_count;
        phys_stage_manifold_build(..., &manifold_count);
        
        // Stage 8: Stabilization
        phys_stab_hint_t *hints = phys_frame_arena_alloc(...);
        phys_stage_stabilization(...);
        
        // Stage 9: Constraint Build
        phys_constraint_t *constraints = phys_frame_arena_alloc(...);
        uint32_t constraint_count;
        phys_stage_constraint_build(..., &constraint_count);
        
        // Stage 10: Island Build
        phys_island_list_t islands;
        phys_stage_island_build(...);
        
        // Stage 11: TGS Solve
        phys_velocity_t *velocities = phys_frame_arena_alloc(...);
        phys_stage_tgs_solve(...);
        
        // Stage 12: Integrate
        phys_stage_integrate(&(phys_integrate_args_t){
            .bodies_in = world->body_pool.bodies_curr,
            .bodies_out = world->body_pool.bodies_next,
            .velocities = velocities,
            .dt = plan.substep_dt,
            ...
        });
        
        // Stage 13: Cache Commit
        phys_stage_cache_commit(...);
        
        // Swap curr/next for next substep
        phys_body_pool_swap_buffers(&world->body_pool);
    }
    
    // Stage 14: Final buffer swap (done in last substep)
    world->tick_count++;
    
    // Expire old cache entries
    phys_manifold_cache_expire(&world->manifold_cache, world->tick_count, 30);
}
```

## Acceptance Criteria

- [ ] All 14 stages executed in order
- [ ] Substep loop runs correct number of times
- [ ] Buffer swap at end of each substep
- [ ] Frame arena reset at start
- [ ] Tick count incremented

## Test Cases

```c
// test_tick_single_body_falls
// Single sphere under gravity: falls correctly

// test_tick_two_bodies_collide
// Two spheres colliding: bounce correctly

// test_tick_multiple_substeps
// Verify 2 substeps = 2x buffer swaps
```
