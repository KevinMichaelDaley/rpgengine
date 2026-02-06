---
id: phys-202
status: closed
deps: [phys-107]
links: [phys-200]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 2.2: Sphere-Capsule Narrowphase


**Parent Epic:** phys-200 (Phase 2: Box and Capsule Colliders)

## Description

Implement sphere-capsule collision detection. Target: ~0.5 µs per pair.

## Files

- `src/physics/collision/narrowphase_sphere_capsule.c`
- `tests/physics/narrowphase_sphere_capsule_tests.c`

## Algorithm

1. Find closest point on capsule's line segment to sphere center
2. Treat as sphere-sphere with capsule radius at that point
3. Generate contact if distance < sphere_radius + capsule_radius

## Test Cases

```c
// test_sphere_vs_capsule_side     (closest to mid-segment)
// test_sphere_vs_capsule_cap      (closest to hemisphere end)
// test_sphere_separated           (no contact)
// test_sphere_capsule_penetrating (overlapping, correct depth)
```

## Acceptance Criteria

- [ ] Correct closest point on capsule segment
- [ ] Contact generation with correct normal and depth
- [ ] Works for both side and cap contacts

