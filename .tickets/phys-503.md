---
id: phys-503
status: closed
deps: [phys-501]
links: [phys-500]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 5.3: Closest Point Query


**Parent Epic:** phys-500 (Phase 5: Raycasts and World Queries)

Find closest point on a body's surface to a given world point.
Used for audio attenuation, AI proximity, etc.

## API

```c
bool phys_closest_point(const phys_world_t *world, phys_vec3_t point,
                        float max_distance, phys_vec3_t *closest_out,
                        uint32_t *body_id_out, uint32_t layer_mask);
```

## Test Cases

```c
// test_closest_point_on_sphere
// test_closest_point_on_box_face
// test_closest_point_on_capsule
```

## Acceptance Criteria

- [ ] Correct closest point for all primitive types
- [ ] Returns false if nothing within max_distance

