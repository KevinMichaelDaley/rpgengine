---
id: rpg-4dpg
status: closed
deps: []
links: []
created: 2026-03-02T12:25:37Z
type: task
priority: 1
assignee: KMD
---
# Parallel tick parity: sub-substeps, XPBD fallback, solver_mode

The parallel tick (tick_parallel.c) is missing several stabilizing features that exist in the serial tick (tick.c). Since the parallel tick is the only path actually used (phys_tick_runner calls phys_world_tick_parallel), these gaps cause visible instability (boxes on edges, jitter).

## Missing features to port from tick.c

1. **Stage 10c: Sub-substeps for fast islands** — Islands with body speed > 5 m/s get up to 16 solver sub-substeps with finer dt. Includes: compute_island_sub_substeps(), sub-substep loop with joint row rebuild, world-space inertia tensor recompute per sub-substep, effective mass recompute, inline per-body integration, body_sub_substepped skip array passed to main integrate stage.

2. **Stage 11b: XPBD Solve** — T2-T4 islands use Extended Position-Based Dynamics instead of TGS. Gathers XPBD constraints, runs phys_stage_xpbd_solve, merges velocities back into shared array.

3. **solver_mode in joint constraint build** — Parallel tick hardcodes 0 (TGS) in phys_joint_build_constraints call. Should use phys_tier_cross_solver_mode to determine correct mode based on body tiers.

4. **skip_body in integrate** — Parallel tick doesn't pass skip_body to phys_stage_integrate_par, so sub-substepped bodies would be double-integrated without it.

## Key files
- `src/physics/world/tick.c` — serial tick with all features (reference)
- `src/physics/world/tick_parallel.c` — parallel tick (target)
- `src/physics/stages/par/tgs_solve_par.c` — parallel TGS solver
- `src/physics/stages/par/integrate_par.c` — parallel integrate (needs skip_body support)
