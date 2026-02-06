---
id: phys-601
status: open
deps: [phys-311]
links: [phys-600]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 6.1: Static BVH Build


**Parent Epic:** phys-600 (Phase 6: Static BVH)

Build BVH from static bodies using SAH (Surface Area Heuristic).
This replaces grid-based broadphase for static geometry, which is
much more efficient for large static worlds.

## Acceptance Criteria

- [ ] SAH-based split produces balanced tree
- [ ] Build time < 50ms for 10k static triangles/bodies

