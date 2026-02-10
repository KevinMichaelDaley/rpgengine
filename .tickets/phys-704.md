---
id: phys-704
status: closed
deps: [phys-701, phys-702, phys-703]
links: [phys-700]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 7.4: Phase 7 Integration Test


**Parent Epic:** phys-700 (Phase 7: Advanced Stability)

## Test Cases

```c
// test_10_sphere_stack_1000_frames     (must not drift > 1mm after 1000 frames)
// test_box_tower_20_high               (planar sleep propagation keeps base sleeping)
// test_fast_object_no_tunnel           (speculative contacts catch 100 m/s projectile)
// test_mixed_pile_100_bodies_stable    (sphere + box + capsule pile settles in < 5s)
```

## Acceptance Criteria

- [ ] All stability tests pass
- [ ] No drift, collapse, or tunneling

