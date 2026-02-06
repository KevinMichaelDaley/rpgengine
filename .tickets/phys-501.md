---
id: phys-501
status: open
deps: [phys-311]
links: [phys-500]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 5.1: Raycast Against Shapes


**Parent Epic:** phys-500 (Phase 5: Raycasts and World Queries)

## Files

- `include/ferrum/physics/raycast.h`
- `src/physics/query/raycast.c`
- `tests/physics/raycast_tests.c`

## API

```c
typedef struct phys_ray_t {
    phys_vec3_t origin;
    phys_vec3_t direction;  // normalized
    float max_distance;
} phys_ray_t;

typedef struct phys_raycast_hit_t {
    float distance;
    phys_vec3_t point;
    phys_vec3_t normal;
    uint32_t body_id;
} phys_raycast_hit_t;

bool phys_raycast(const phys_world_t *world, const phys_ray_t *ray,
                  phys_raycast_hit_t *hit, uint32_t layer_mask);
uint32_t phys_raycast_all(const phys_world_t *world, const phys_ray_t *ray,
                          phys_raycast_hit_t *hits, uint32_t max_hits,
                          uint32_t layer_mask);
```

Ray-sphere, ray-box, ray-capsule primitive tests.
Uses spatial grid for broadphase culling.

## Test Cases

```c
// test_ray_hits_sphere
// test_ray_misses_sphere
// test_ray_hits_box_face
// test_ray_hits_capsule
// test_ray_all_multiple_hits
// test_ray_respects_max_distance
// test_ray_returns_closest_hit
```

## Acceptance Criteria

- [ ] Correct ray-primitive intersection for all 3 types
- [ ] Returns closest hit by default
- [ ] Layer mask filtering works
- [ ] Uses broadphase to avoid testing all bodies

