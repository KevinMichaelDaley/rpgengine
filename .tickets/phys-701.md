---
id: phys-701
status: closed
deps: [phys-408]
links: [phys-700]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 7.1: Manifold Point Reduction


**Parent Epic:** phys-700 (Phase 7: Advanced Stability)

Keep best 4 contact points per manifold using deepest-point + maximum-area
algorithm. Reduces constraint count without losing contact quality.

## Acceptance Criteria

- [ ] Manifold reduced to ≤4 points
- [ ] Deepest point always kept
- [ ] Remaining points maximize contact area

