---
id: phys-309
status: open
deps: [phys-301]
links: [phys-300]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 3.9: Parallelize TGS Solve (T0/T1)


**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

Parallelize TGS solve over islands. Each island is one job (or split
large islands into batched constraint groups). Islands are independent
so no synchronization needed between jobs.

## Acceptance Criteria

- [ ] One job per island (or batched for large islands)
- [ ] Identical convergence to single-threaded
- [ ] Deterministic results

