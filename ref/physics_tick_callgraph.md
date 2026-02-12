# Physics Tick Call Graph (Current)

This document describes the **current** physics tick orchestration as implemented in the codebase.

Source of truth:
- `src/physics/world/tick.c` — single-threaded reference tick
- `src/physics/world/tick_parallel.c` — parallel tick (dispatches stage work via `phys_job_context_t`)

Public entry points:
```c
void phys_world_tick(phys_world_t *world, const phys_game_state_t *game);
void phys_world_tick_parallel(phys_world_t *world,
                              const phys_game_state_t *game,
                              phys_job_context_t *jobs);
```

Important current-status notes:
- **XPBD is not currently invoked** by `tick.c` / `tick_parallel.c` (modules exist, but the orchestrators only run TGS).
- **Position projection + velocity sync are not separate stages**; they are fused into **Stage 11 (TGS)** via **split impulse** using a `pseudo_velocities[]` buffer.
- **Halo closure and broadphase run once per tick** (outside the substep loop).
- **Joints are implemented**: distance, ball, hinge. Joint constraints are built each substep and solved by TGS alongside contacts.
- **Adaptive solver iterations**: per-island iteration count scales with max body speed (sqrt ramp, up to 10× base).
- **Solver sub-substeps**: fast islands (15–150 m/s) get up to 8 sub-substeps with inline per-body integration. Joint constraints are rebuilt each sub-substep; contact constraints stay frozen.
- **Nonlinear joint projection**: after TGS iterations, 4 passes recompute world anchors from predicted state and apply coupled position + angular corrections to pseudo-velocities. Only the narrowphase/solve/integrate portion repeats per-substep.

---

## Persistent vs transient state (high level)

Persistent (`phys_world_t`, lives across ticks):
- `world->body_pool.bodies_curr` / `world->body_pool.bodies_next` (double-buffer)
- `world->aabbs[]`
- `world->manifold_cache`
- shape pools (`world->spheres`, `world->boxes`, `world->capsules`)
- `world->joints[]`, `world->joint_count` (joint descriptors)

Transient (arena allocated from `world->frame_arena`, reset once per tick):
- `active[]` flags mirror `world->body_pool.active[]`
- `tier_lists`
- spatial grid + broadphase `pairs[]`
- `body_sub_substepped[]` (skip mask for main integrate)
- per-substep: `candidates[]`, `manifolds[]`, `hints[]`, `constraints[]`, `islands`, `velocities[]`, `pseudo_velocities[]`, `body_max_pen[]`
- per-sub-substep (fast islands): `ss_vel[]`, `ss_pseudo[]`, sub-substep island list

---

## Stage order (matches `tick.c` / `tick_parallel.c`)

Numbering here matches the stage labels in the tick orchestrators.

**Once per tick (outside substep loop):**
0. Step plan — `phys_stage_step_plan()`
1. Tier classify — `phys_stage_tier_classify()` / `phys_stage_tier_classify_par()`
2. Spatial update (build AABBs + spatial grid) — `phys_stage_spatial_update()` / `_par()`
3. Halo closure (tier promotion) — `phys_stage_halo_closure()`
5. Broadphase — `phys_stage_broadphase()` / `_par()`

**Repeated per-substep (`sub = 0..max_substeps-1`):**
4. AABB update — `phys_stage_aabb_update()` (**skipped for substep 0**)
6. Narrowphase — `phys_stage_narrowphase()` / `_par()` (**skipped in `world->prediction_mode`**)
7. Manifold build + warmstart — `phys_stage_manifold_build()` / `_par()` (**skipped in prediction mode**)
8. Stabilization hints — `phys_stage_stabilization()` / `_par()` (**skipped in prediction mode**)
9. Constraint build — `phys_stage_constraint_build()` / `_par()` (**skipped in prediction mode**)
10. Island build — `phys_stage_island_build()` (**skipped in prediction mode**)
11. TGS solve (+ split impulse + nonlinear joint projection) — `phys_stage_tgs_solve()` / `_par()` (**skipped in prediction mode**)
    - Adaptive per-island iterations (sqrt ramp based on max body speed)
    - Speed-dependent Baumgarte leak for joint constraints
    - Per-row viscous damping for joints
    - Nonlinear joint position projection after iterations (4 passes)
12. Integrate — `phys_stage_integrate()` / `_par()` (always; prediction mode seeds velocities from bodies)
    - Bodies marked as sub-substepped are skipped (already integrated inline)
13. Cache commit — `phys_stage_cache_commit()` (**skipped in prediction mode**)
    - Buffer swap after each substep — `phys_body_pool_swap_buffers(&world->body_pool)`

**Epilogue (after substep loop):**
- `world->tick_count++`
- `phys_manifold_cache_expire(&world->manifold_cache, world->tick_count, 30)`

---

## High-level call graph

```text
phys_world_tick(world, game)
  ├─ world->impact_event_count = 0
  ├─ phys_frame_arena_reset(&world->frame_arena)
  ├─ Stage 0: phys_stage_step_plan(&plan, world, game)
  ├─ active[] = copy(world->body_pool.active[])
  ├─ Stage 1: phys_stage_tier_classify(... -> tier_lists)
  ├─ Stage 2: phys_stage_spatial_update(... -> world->aabbs, grid)
  ├─ Stage 3: phys_stage_halo_closure(... tier_lists, grid)
  ├─ Stage 5: phys_stage_broadphase(... grid, tier_lists -> pairs[], pair_count)
  └─ for sub in [0..max_substeps):
       ├─ Stage 4: if (sub>0) phys_stage_aabb_update(...)
       ├─ if (!world->prediction_mode):
       │    ├─ Stage 6: phys_stage_narrowphase(... pairs -> candidates[])
       │    ├─ Stage 7: phys_stage_manifold_build(... candidates -> manifolds[])
       │    ├─ Stage 8: phys_stage_stabilization(... manifolds -> hints[])
       │    ├─ Stage 9: phys_stage_constraint_build(... -> constraints[])
       │    │    └─ includes joint constraint build (phys_joint_build_* + phys_joint_build_constraints)
       │    ├─ body_max_pen[] computed from constraints (sleep blocking)
       │    ├─ Stage 10: phys_stage_island_build(... -> islands)
       │    ├─ [Sub-substeps for fast islands]:
       │    │    └─ for islands with max speed > 15 m/s:
       │    │         ├─ compute sub-substep count (1–8, sqrt ramp)
       │    │         ├─ for each sub-substep:
       │    │         │    ├─ rebuild joint constraints for sub-substep dt
       │    │         │    ├─ TGS solve (adaptive iterations + nonlinear projection)
       │    │         │    └─ inline per-body integration on bodies_curr
       │    │         └─ mark bodies as sub-substepped (skip in main integrate)
       │    ├─ Stage 11: phys_stage_tgs_solve(... -> velocities[], pseudo_velocities[])
       │    │    ├─ adaptive per-island iteration count
       │    │    ├─ speed-dependent Baumgarte leak for joints
       │    │    ├─ per-row viscous damping for joints
       │    │    └─ project_joints_nonlinear(joints, bodies, pseudo, dt)
       │    └─ Stage 13: phys_stage_cache_commit(... -> impact_events)
       ├─ Stage 12: phys_stage_integrate(
       │      bodies_curr + velocities (+ pseudo_velocities) -> bodies_next)
       ├─ phys_body_pool_swap_buffers(&world->body_pool)
       └─ (next substep)
  ├─ world->tick_count++
  └─ phys_manifold_cache_expire(...)

phys_world_tick_parallel(world, game, jobs)
  Same stage order as above, but uses *_par() variants for parallelizable stages
  and uses `phys_job_context_t` to dispatch + wait per stage.
```

---

## Prediction mode behavior (`world->prediction_mode`)

When `prediction_mode` is enabled, the tick skips stages 6–11 and 13.
Integration still runs; velocities are seeded from `bodies_curr[*].linear_vel/angular_vel` and gravity is applied in the tick orchestrator (because normal gravity integration is part of TGS initialization).

---

## Where split impulse lives

Split impulse position correction is implemented by:
- allocating `pseudo_velocities[]` each substep
- producing position-level corrections in `phys_stage_tgs_solve()` without polluting body velocities
- consuming `pseudo_velocities[]` in `phys_stage_integrate()`

After the iterative TGS solve, **nonlinear joint position projection** runs:
- `project_joints_nonlinear()` (in tgs_solve.c and tgs_solve_par.c)
- For each joint, predicts body state from pos + pseudo*dt, integrates orientation by pseudo angular vel
- Recomputes world anchors from predicted state
- If residual anchor error > 1cm, applies coupled corrections to pseudo-velocities:
  - Linear: translational correction weighted by inverse mass
  - Angular: `r×e/|r|²` rotation correction to swing lever arms toward targets
- Runs 4 passes (iterative refinement), correcting 80% of error per pass

See the in-code note in:
- `src/physics/solver/tgs_solve.c`
- `src/physics/stages/par/tgs_solve_par.c`
- `src/physics/world/tick.c`
- `src/physics/world/tick_parallel.c`
