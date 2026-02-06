---
id: phys-206
status: open
deps: [phys-004, phys-003]
links: [phys-200]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 2.6: AABB for Rotated Box and Capsule


**Parent Epic:** phys-200 (Phase 2: Box and Capsule Colliders)

## Description

Ensure AABB computation handles rotated box and capsule colliders correctly.
The AABB must be a tight world-aligned bounding box of the rotated shape.

## Files

- `src/physics/collision/aabb.c` (extend existing)
- `tests/physics/aabb_tests.c` (extend existing)

## Test Cases

```c
// test_aabb_box_rotated_45_y       (box rotated 45° around Y, expanded X/Z)
// test_aabb_capsule_rotated_90     (capsule on its side, AABB changes shape)
// test_aabb_compound_encloses_all  (compound collider AABB covers all children)
```

## Acceptance Criteria

- [ ] Rotated box AABB is tight (not using unrotated half_extents)
- [ ] Rotated capsule AABB accounts for hemisphere ends

