---
id: phys-103
status: closed
deps: [phys-013]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.3: Spatial Index Update Stage (Stage 2)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 2: Spatial Index Update. Computes AABBs for all bodies and
populates the spatial hash grid.

## Files to create

- `include/ferrum/physics/spatial_update.h`
- `src/physics/stages/spatial_update.c`
- `tests/physics/spatial_update_tests.c`

## API

```c
typedef struct phys_spatial_update_args_t {
    const phys_body_t *bodies;
    const phys_collider_t *colliders;
    const phys_sphere_t *spheres;
    const phys_box_t *boxes;
    const phys_capsule_t *capsules;
    phys_aabb_t *aabbs_out;
    phys_spatial_grid_t *grid_out;
    uint32_t body_count;
} phys_spatial_update_args_t;

void phys_stage_spatial_update(const phys_spatial_update_args_t *args);
```

## Implementation

```c
void phys_stage_spatial_update(const phys_spatial_update_args_t *args) {
    phys_spatial_grid_clear(args->grid_out);
    
    for (uint32_t i = 0; i < args->body_count; ++i) {
        const phys_body_t *body = &args->bodies[i];
        const phys_collider_t *collider = &args->colliders[i];
        phys_aabb_t *aabb = &args->aabbs_out[i];
        
        // Compute world-space AABB based on collider type
        phys_vec3_t center = phys_collider_world_center(collider, body->position, body->orientation);
        phys_quat_t rotation = phys_collider_world_rotation(collider, body->orientation);
        
        switch (collider->type) {
            case PHYS_SHAPE_SPHERE: {
                float radius = args->spheres[collider->shape_index].radius;
                phys_aabb_from_sphere(aabb, center, radius);
                break;
            }
            case PHYS_SHAPE_BOX: {
                phys_vec3_t he = args->boxes[collider->shape_index].half_extents;
                phys_aabb_from_box(aabb, center, rotation, he);
                break;
            }
            case PHYS_SHAPE_CAPSULE: {
                const phys_capsule_t *cap = &args->capsules[collider->shape_index];
                phys_aabb_from_capsule(aabb, center, rotation, cap->radius, cap->half_height);
                break;
            }
            default:
                // Empty AABB for unknown types
                *aabb = (phys_aabb_t){{0,0,0}, {0,0,0}};
                break;
        }
        
        // Insert into spatial grid
        phys_spatial_grid_insert(args->grid_out, i, aabb);
    }
}
```

## Acceptance Criteria

- [ ] AABBs computed correctly for all collider types
- [ ] Grid populated with all body indices
- [ ] Handles bodies with no collider gracefully

## Test Cases

```c
// test_spatial_update_spheres
phys_body_t bodies[3];
phys_collider_t colliders[3];
phys_sphere_t spheres[3] = {{.radius = 1.0f}, {.radius = 0.5f}, {.radius = 2.0f}};
phys_aabb_t aabbs[3];

for (int i = 0; i < 3; ++i) {
    phys_body_init(&bodies[i]);
    bodies[i].position = (phys_vec3_t){i * 10.0f, 0, 0};
    phys_collider_init_sphere(&colliders[i], i, (phys_vec3_t){0,0,0});
}

phys_frame_arena_t arena;
phys_frame_arena_init(&arena, 1024 * 1024);
phys_spatial_grid_t grid;
phys_spatial_grid_init(&grid, 256, 10.0f, &arena);

phys_spatial_update_args_t args = {
    .bodies = bodies,
    .colliders = colliders,
    .spheres = spheres,
    .aabbs_out = aabbs,
    .grid_out = &grid,
    .body_count = 3
};

phys_stage_spatial_update(&args);

// Verify AABBs
ASSERT_VEC3_NEAR(aabbs[0].min, (phys_vec3_t){-1, -1, -1}, 0.001f);
ASSERT_VEC3_NEAR(aabbs[0].max, (phys_vec3_t){1, 1, 1}, 0.001f);

// Verify grid query finds body 0
uint32_t results[10];
uint32_t count = phys_spatial_grid_query(&grid, &aabbs[0], results, 10);
ASSERT(count >= 1);

phys_frame_arena_destroy(&arena);
```
