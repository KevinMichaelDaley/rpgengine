---
id: phys-902
status: open
deps: [phys-901]
links: [phys-900]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 9.2: Primitive-vs-Mesh Narrowphase


**Parent Epic:** phys-900 (Phase 9: Mesh Colliders)

Sphere/box/capsule vs triangle narrowphase. Traverses mesh BVH
to find candidate triangles, then tests each primitive pair.

## Acceptance Criteria

- [ ] All 3 primitive types can collide with mesh triangles
- [ ] BVH traversal limits triangle tests to relevant subset

