---
id: phys-203
status: open
deps: [phys-107]
links: [phys-200]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 2.3: Box-Box Narrowphase (SAT)


**Parent Epic:** phys-200 (Phase 2: Box and Capsule Colliders)

## Description

Implement box-box collision detection using SAT (Separating Axis Theorem)
with 15 test axes and contact clipping. Target: ~1.5 µs per pair.

## Files

- `src/physics/collision/narrowphase_box_box.c`
- `tests/physics/narrowphase_box_box_tests.c`

## Algorithm

1. Test 15 separating axes: 3 face normals A + 3 face normals B + 9 edge cross products
2. Track minimum penetration axis
3. If no separating axis found, generate contact manifold via Sutherland-Hodgman clipping
4. Reduce to ≤4 contact points

## Test Cases

```c
// test_box_box_face_contact        (flat face-to-face, 4 contact points)
// test_box_box_edge_contact        (edge-edge, 1-2 contact points)
// test_box_box_corner_contact      (corner touching face)
// test_box_box_separated           (no contact)
// test_box_box_rotated_45          (rotated box on floor)
// test_box_box_stacking            (stable 3-box stack)
```

## Acceptance Criteria

- [ ] SAT correctly identifies separating axis
- [ ] Contact clipping produces correct manifold
- [ ] ≤4 contact points per manifold
- [ ] Feature IDs enable persistent tracking across frames

