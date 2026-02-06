---
id: phys-302
status: open
deps: [phys-301]
links: [phys-300]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 3.2: Parallelize Tier Classification


**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

Split body range across jobs (1k bodies/job). Each job classifies its
range independently. Merge tier lists after all jobs complete.

## Acceptance Criteria

- [ ] Identical results to single-threaded classification
- [ ] Scales linearly with body count

