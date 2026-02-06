---
id: phys-201
status: closed
deps: [phys-107]
links: [phys-200]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 2.1: Sphere-Box Narrowphase


**Parent Epic:** phys-200 (Phase 2: Box and Capsule Colliders)

## Description

Implement sphere-box collision detection. Target: ~0.8 µs per pair.

## Files

- `src/physics/collision/narrowphase_sphere_box.c`
- `tests/physics/narrowphase_sphere_box_tests.c`

## Algorithm

1. Transform sphere center to box local space
2. Find closest point on box surface (clamp to half_extents)
3. Compute distance and penetration depth
4. Generate contact point and normal in world space

## Test Cases

```c
// test_sphere_inside_box_face     (sphere center projects onto face)
// test_sphere_touching_box_edge   (closest point is on edge)
// test_sphere_touching_box_corner (closest point is corner)
// test_sphere_separated_from_box  (no contact generated)
// test_sphere_resting_on_box_top  (penetration = 0, normal = +Y)
```

## Acceptance Criteria

- [ ] Correct contact point, normal, penetration for face/edge/corner cases
- [ ] Normal points from sphere to box
- [ ] Feature ID assigned for persistent tracking
- [ ] No contact generated for separated shapes

