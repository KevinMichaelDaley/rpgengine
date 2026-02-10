---
id: phys-602
status: in_progress
deps: [phys-601]
links: [phys-600]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 6.2: BVH Query


**Parent Epic:** phys-600 (Phase 6: Static BVH)

Query BVH for dynamic-vs-static broadphase. AABB traversal
with early termination.

## Acceptance Criteria

- [ ] Correct pair generation for dynamic vs static bodies
- [ ] O(log n) per query

