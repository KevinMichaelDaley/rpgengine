---
id: phys-204
status: closed
deps: [phys-107]
links: [phys-200]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 2.4: Box-Capsule Narrowphase


**Parent Epic:** phys-200 (Phase 2: Box and Capsule Colliders)

## Description

Implement box-capsule collision detection. Target: ~1.2 µs per pair.

## Files

- `src/physics/collision/narrowphase_box_capsule.c`
- `tests/physics/narrowphase_box_capsule_tests.c`

## Algorithm

1. Find closest point on capsule segment to box
2. Use box face/edge closest point computation
3. Generate contact if distance < capsule_radius

## Test Cases

```c
// test_capsule_resting_on_box      (capsule lying on box top face)
// test_capsule_edge_vs_box_edge    (edge-edge contact)
// test_capsule_end_vs_box_face     (hemisphere touching face)
// test_capsule_separated           (no contact)
```

## Acceptance Criteria

- [ ] Correct contact for face, edge, and cap contacts
- [ ] Normal points from capsule to box

