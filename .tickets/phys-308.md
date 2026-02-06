---
id: phys-308
status: open
deps: [phys-301]
links: [phys-300]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 3.8: Parallelize Constraint Build


**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

Parallelize constraint build. Each job builds constraints for a range
of manifolds. Includes constraint specialization dispatch
(planar/sphere/generic) in parallel.

## Acceptance Criteria

- [ ] Identical constraints to single-threaded
- [ ] Specialization dispatch works correctly in parallel

