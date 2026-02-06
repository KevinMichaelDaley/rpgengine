---
id: phys-304
status: closed
deps: [phys-301]
links: [phys-300]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 3.4: Parallelize Broadphase


**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

Parallelize broadphase pair generation. Each job processes a range of
grid cells. Thread-local pair buffers merged into final pair list.

## Acceptance Criteria

- [ ] Identical pair list (modulo order) to single-threaded
- [ ] Deterministic output order (stable sort by cell ID)

