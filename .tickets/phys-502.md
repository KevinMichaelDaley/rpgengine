---
id: phys-502
status: open
deps: [phys-501]
links: [phys-500]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 5.2: Shape Overlap Query


**Parent Epic:** phys-500 (Phase 5: Raycasts and World Queries)

Test if a shape at a given position overlaps any bodies.
Used for spawn validation, trigger volumes, etc.

## API

```c
uint32_t phys_overlap(const phys_world_t *world, const phys_collider_t *shape,
                      phys_vec3_t position, phys_quat_t rotation,
                      uint32_t *body_ids_out, uint32_t max_results,
                      uint32_t layer_mask);
```

## Test Cases

```c
// test_overlap_sphere_finds_bodies
// test_overlap_box_rotated
// test_overlap_returns_nothing_for_empty_region
```

## Acceptance Criteria

- [ ] Correct overlap detection for all primitive types
- [ ] Uses broadphase for culling

