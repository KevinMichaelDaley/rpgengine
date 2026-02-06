---
id: phys-303
status: closed
deps: [phys-301]
links: [phys-300]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 3.3: Parallelize Spatial Update


**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

Parallelize spatial grid cell insertion. Use per-cell atomic lists or
thread-local buffers merged after completion.

## Acceptance Criteria

- [ ] Identical grid state to single-threaded
- [ ] No data races (TSan clean)

