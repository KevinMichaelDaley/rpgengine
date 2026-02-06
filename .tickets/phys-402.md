---
id: phys-402
status: open
deps: [phys-401]
links: [phys-400]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 4.2: Per-Tier Solver Parameters


**Parent Epic:** phys-400 (Phase 4: Tiered Simulation)

## Description

Different solver mode, substeps, and iterations per tier:
- T0: TGS, 3 substeps, 24 iterations
- T1: TGS, 2 substeps, 20 iterations
- T2: Jacobi XPBD, 1 substep, 8 iterations, compliance 1e-6
- T3: Jacobi XPBD, 1 substep, 4 iterations, compliance 1e-5, sphere simplify if ratio < 1.3
- T4: Jacobi XPBD, amortized (10 Hz), 2 iterations, compliance 1e-4, sphere preferred

The constraint build stage sets `solver_mode` on each constraint based on
the tier of the involved bodies. Cross-tier constraints use TGS if either
body is T0/T1.

## Acceptance Criteria

- [ ] Per-tier iteration counts applied correctly
- [ ] Cross-tier constraints use higher-fidelity solver
- [ ] T3/T4 reduced iterations don't cause visible instability at distance

