---
id: phys-310
status: closed
deps: [phys-301]
links: [phys-300]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 3.10: Parallelize Integrate


**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

Parallelize integration + sleep detection. Per-body, no dependencies.
Write to bodies_next buffer (separate from bodies_curr).

## Acceptance Criteria

- [ ] Identical positions/velocities to single-threaded
- [ ] Sleep detection works correctly in parallel

