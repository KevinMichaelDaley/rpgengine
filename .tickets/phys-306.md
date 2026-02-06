---
id: phys-306
status: closed
deps: [phys-301]
links: [phys-300]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 3.6: Parallelize Manifold Build


**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

Parallelize manifold build + cache merge. 32 pairs per job.
Cache access must be synchronized (per-pair locks or partitioned).

## Acceptance Criteria

- [ ] Warmstart impulses correctly preserved
- [ ] No data races on manifold cache

