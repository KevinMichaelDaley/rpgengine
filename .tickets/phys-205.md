---
id: phys-205
status: closed
deps: [phys-107]
links: [phys-200]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 2.5: Capsule-Capsule Narrowphase


**Parent Epic:** phys-200 (Phase 2: Box and Capsule Colliders)

## Description

Implement capsule-capsule collision detection. Target: ~1.0 µs per pair.

## Files

- `src/physics/collision/narrowphase_capsule_capsule.c`
- `tests/physics/narrowphase_capsule_capsule_tests.c`

## Algorithm

1. Find closest points between two line segments
2. Treat as sphere-sphere with respective radii at those points
3. Generate contact if distance < radius_a + radius_b

## Test Cases

```c
// test_capsules_parallel           (side-by-side, single contact)
// test_capsules_crossing           (X configuration)
// test_capsules_end_to_end         (cap-to-cap contact)
// test_capsules_separated          (no contact)
```

## Acceptance Criteria

- [ ] Correct closest-point-on-segment computation
- [ ] Contact generation with correct depth and normal
- [ ] Handles degenerate cases (parallel, coincident)

