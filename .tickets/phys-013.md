---
id: phys-013
status: open
deps: [phys-012]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.13: Phase 0 Integration Test

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

End-to-end test verifying all Phase 0 data structures work together.
No simulation yet—just allocation, initialization, and cleanup.

## Files to create

- `tests/physics/phase0_integration_tests.c`

## Test Cases

```c
// test_create_world_with_many_bodies
phys_world_config_t cfg = phys_world_config_default();
cfg.max_bodies = 1000;
cfg.frame_arena_size = 4 * 1024 * 1024;

phys_world_t world;
phys_world_init(&world, &cfg);

// Create 100 bodies with different collider types
pool_handle_t bodies[100];
for (int i = 0; i < 100; ++i) {
    bodies[i] = phys_world_create_body(&world);
    phys_body_t *b = phys_world_get_body(&world, bodies[i]);
    b->position = (phys_vec3_t){i * 2.0f, 0, 0};
    phys_body_set_mass(b, 1.0f + i * 0.1f);
    
    // Alternate collider types
    switch (i % 3) {
        case 0:
            phys_world_set_sphere_collider(&world, bodies[i], 0.5f, (phys_vec3_t){0,0,0});
            phys_body_set_sphere_inertia(b, b->inv_mass > 0 ? 1.0f / b->inv_mass : 0, 0.5f);
            break;
        case 1:
            phys_world_set_box_collider(&world, bodies[i], 
                (phys_vec3_t){0.5f, 0.5f, 0.5f}, (phys_vec3_t){0,0,0}, PHYS_QUAT_IDENTITY);
            phys_body_set_box_inertia(b, b->inv_mass > 0 ? 1.0f / b->inv_mass : 0, 
                (phys_vec3_t){0.5f, 0.5f, 0.5f});
            break;
        case 2:
            phys_world_set_capsule_collider(&world, bodies[i], 
                0.3f, 0.5f, (phys_vec3_t){0,0,0}, PHYS_QUAT_IDENTITY);
            phys_body_set_capsule_inertia(b, b->inv_mass > 0 ? 1.0f / b->inv_mass : 0, 0.3f, 0.5f);
            break;
    }
}

ASSERT(phys_world_body_count(&world) == 100);

// Verify all colliders are set correctly
for (int i = 0; i < 100; ++i) {
    const phys_collider_t *c = phys_world_get_collider(&world, bodies[i]);
    ASSERT(c != NULL);
    ASSERT(c->type == (phys_shape_type_t)(i % 3));
}

phys_world_destroy(&world);
// Run with valgrind/ASan to verify no leaks

// test_frame_arena_used_by_tier_lists
phys_world_init(&world, &cfg);

// Tier lists should allocate from frame arena
size_t arena_before = phys_frame_arena_used(&world.frame_arena);

phys_tier_lists_t tier_lists;
phys_tier_lists_init(&tier_lists, &world.frame_arena, 1000);

size_t arena_after = phys_frame_arena_used(&world.frame_arena);
ASSERT(arena_after > arena_before);

// Reset should free tier list memory
phys_frame_arena_reset(&world.frame_arena);
ASSERT(phys_frame_arena_used(&world.frame_arena) == 0);

phys_world_destroy(&world);

// test_manifold_cache_integrated
phys_world_init(&world, &cfg);

// Create two bodies
pool_handle_t h1 = phys_world_create_body(&world);
pool_handle_t h2 = phys_world_create_body(&world);

// Get body indices (implementation-dependent)
uint32_t idx1 = pool_handle_index(h1);
uint32_t idx2 = pool_handle_index(h2);

// Create manifold in cache
phys_manifold_t *m = phys_manifold_cache_get_or_create(&world.manifold_cache, idx1, idx2, 1);
ASSERT(m != NULL);
m->friction = 0.5f;

// Should be findable
phys_manifold_t *found = phys_manifold_cache_find(&world.manifold_cache, idx1, idx2);
ASSERT(found == m);
ASSERT(found->friction == 0.5f);

phys_world_destroy(&world);

// test_spatial_grid_integrated
phys_world_init(&world, &cfg);

// Create bodies with colliders
for (int i = 0; i < 10; ++i) {
    pool_handle_t h = phys_world_create_body(&world);
    phys_body_t *b = phys_world_get_body(&world, h);
    b->position = (phys_vec3_t){i * 5.0f, 0, 0};
    phys_world_set_sphere_collider(&world, h, 1.0f, (phys_vec3_t){0,0,0});
}

// Initialize and populate spatial grid
phys_spatial_grid_t grid;
phys_spatial_grid_init(&grid, 256, 10.0f, &world.frame_arena);

// This test just verifies the structures work together
// Actual population happens in Phase 1 stages

phys_world_destroy(&world);
```

## Acceptance Criteria

- [ ] All data structures initialize correctly
- [ ] Mixed collider types work together
- [ ] Arena allocations don't leak
- [ ] Manifold cache integrates with world
- [ ] No memory leaks (verified with ASan/valgrind)
- [ ] No undefined behavior (verified with UBSan)
