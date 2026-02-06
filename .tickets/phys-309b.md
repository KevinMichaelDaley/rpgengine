---
id: phys-309b
status: open
deps: [phys-301]
links: [phys-300]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 3.9b: Parallelize XPBD Solve (T2–T4)


**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

Parallelize Jacobi XPBD solve. 128 bodies per job. No island decomposition
needed—Jacobi iteration is inherently parallel (read from start-of-iteration
positions, accumulate corrections, blend at end of iteration).

Runs concurrently with TGS solve (Stage 11a and 11b are independent).

## Acceptance Criteria

- [ ] 128 bodies/job batch size
- [ ] Concurrent with TGS solve on different threads
- [ ] Unconditionally stable under contention

