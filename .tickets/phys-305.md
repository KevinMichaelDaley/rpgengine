---
id: phys-305
status: closed
deps: [phys-301]
links: [phys-300]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 3.5: Parallelize Narrowphase


**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

Parallelize narrowphase. 64 pairs per job. Each job writes contacts to
a thread-local arena segment.

## Acceptance Criteria

- [ ] Identical contacts to single-threaded (within float tolerance)
- [ ] 64 pairs/job batch size

