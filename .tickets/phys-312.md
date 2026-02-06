---
id: phys-312
status: open
deps: [phys-301, phys-302, phys-303, phys-304, phys-305, phys-306, phys-307, phys-308, phys-309, phys-309b, phys-310]
links: [phys-300]
created: 2026-02-06T11:47:00.000000000-08:00
type: task
priority: 1
---
# Step 3.12: Parallel Tick Entry Point

**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

## Description

Create the parallel tick orchestrator that replaces the sequential
`phys_world_tick()` with a job-dispatched pipeline. This is the top-level
function that wires all parallelized stages together using job dispatch +
counter barriers, as specified in the physics callgraph (lines 1040–1182).

The sequential tick (phys-115) calls each stage function directly. The
parallel tick instead dispatches each stage as jobs, waits on counter
barriers between stages, and runs TGS (11a) and XPBD (11b) concurrently
on separate job batches.

## Files

- `include/ferrum/physics/tick.h` (extend — add parallel entry point)
- `src/physics/world/tick_parallel.c`
- `tests/physics/tick_parallel_tests.c`

## API

```c
/* Parallel tick entry point — requires job system.
 * Dispatches pipeline stages as jobs with counter-based barriers.
 * Produces identical results to phys_world_tick() (deterministic). */
void phys_world_tick_parallel(phys_world_t *world,
                              const phys_game_state_t *game,
                              phys_job_context_t *jobs);
```

## Implementation Outline

```c
void phys_world_tick_parallel(phys_world_t *world,
                              const phys_game_state_t *game,
                              phys_job_context_t *jobs)
{
    phys_frame_arena_reset(&world->frame_arena);

    // Stage 0: Step Plan [SYNC — single call, no dispatch]
    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, world, game);

    // Stage 1: Tier Classification [PARALLEL — 1024 bodies/job]
    phys_dispatch_stage(jobs, PHYS_STAGE_TIER_CLASSIFY, ...);
    phys_wait_stage(jobs, PHYS_STAGE_TIER_CLASSIFY);

    // Stage 2: Spatial Update [PARALLEL — 512 bodies/job]
    phys_dispatch_stage(jobs, PHYS_STAGE_SPATIAL_UPDATE, ...);
    phys_wait_stage(jobs, PHYS_STAGE_SPATIAL_UPDATE);

    for (uint32_t substep = 0; substep < plan.substeps; ++substep) {
        // Stage 3: Halo Closure [SMALL PARALLEL — 1 per T0 body]
        phys_dispatch_stage(jobs, PHYS_STAGE_HALO, ...);
        phys_wait_stage(jobs, PHYS_STAGE_HALO);

        // Stage 4: AABB Update [PARALLEL]
        phys_dispatch_stage(jobs, PHYS_STAGE_AABB, ...);
        phys_wait_stage(jobs, PHYS_STAGE_AABB);

        // Stage 5: Broadphase [PARALLEL — cell partitioned]
        phys_dispatch_stage(jobs, PHYS_STAGE_BROADPHASE, ...);
        phys_wait_stage(jobs, PHYS_STAGE_BROADPHASE);

        // Stage 6: Narrowphase [PARALLEL — 64 pairs/job]
        phys_dispatch_stage(jobs, PHYS_STAGE_NARROWPHASE, ...);
        phys_wait_stage(jobs, PHYS_STAGE_NARROWPHASE);

        // Stage 7: Manifold Build [PARALLEL — 32 candidates/job]
        phys_dispatch_stage(jobs, PHYS_STAGE_MANIFOLD, ...);
        phys_wait_stage(jobs, PHYS_STAGE_MANIFOLD);

        // Stage 8: Stabilization [PARALLEL — 64 manifolds/job]
        phys_dispatch_stage(jobs, PHYS_STAGE_STABILIZE, ...);
        phys_wait_stage(jobs, PHYS_STAGE_STABILIZE);

        // Stage 9: Constraint Build [PARALLEL]
        phys_dispatch_stage(jobs, PHYS_STAGE_CONSTRAINT, ...);
        phys_wait_stage(jobs, PHYS_STAGE_CONSTRAINT);

        // Stage 10: Island Build [SYNC — union-find, ~50 µs]
        phys_stage_island_build(...);

        // Stage 11a + 11b: TGS + XPBD [CONCURRENT]
        // Dispatch both, wait on both
        phys_dispatch_stage(jobs, PHYS_STAGE_TGS_SOLVE, ...);
        phys_dispatch_stage(jobs, PHYS_STAGE_XPBD_SOLVE, ...);
        phys_wait_stage(jobs, PHYS_STAGE_TGS_SOLVE);
        phys_wait_stage(jobs, PHYS_STAGE_XPBD_SOLVE);

        // Stage 12: Integrate [PARALLEL — 512 bodies/job]
        phys_dispatch_stage(jobs, PHYS_STAGE_INTEGRATE, ...);
        phys_wait_stage(jobs, PHYS_STAGE_INTEGRATE);

        // Stage 13: Cache Commit [SYNC — ~20 µs]
        phys_stage_cache_commit(...);

        // Buffer swap for next substep
        phys_body_pool_swap_buffers(&world->body_pool);
    }

    world->tick_count++;
    phys_manifold_cache_expire(&world->manifold_cache, world->tick_count, 30);
}
```

## Key Design Points

- **Deterministic:** Must produce bit-exact results vs `phys_world_tick()`.
  Job dispatch order and merge order must be deterministic.
- **Concurrent 11a/11b:** TGS and XPBD solves run on different threads
  simultaneously. They operate on disjoint body/constraint sets (T0/T1 vs
  T2–T4), so no synchronization needed between them.
- **Sync stages stay sync:** Step plan (Stage 0), island build (Stage 10),
  and cache commit (Stage 13) run on the calling thread directly.
- **Batch sizes:** Match callgraph spec — 1024 (tier), 512 (spatial, integrate),
  64 (narrowphase, stabilize), 32 (manifold), per-island (TGS), 128 (XPBD).

## Acceptance Criteria

- [ ] `phys_world_tick_parallel()` produces identical results to sequential tick
- [ ] All parallel stages dispatched via `phys_job_context_t`
- [ ] Counter barriers between every stage pair
- [ ] TGS and XPBD solve concurrently
- [ ] Sync stages (0, 10, 13) not dispatched as jobs
- [ ] TSan clean under high contention
- [ ] Benchmark: measurable speedup vs sequential on 4+ threads

## Test Cases

```c
// test_parallel_tick_matches_sequential
// Run same scenario through phys_world_tick() and phys_world_tick_parallel()
// Results must be bit-exact for all body positions/velocities

// test_parallel_tick_concurrent_solvers
// Scenario with both T0/T1 bodies (TGS) and T2+ bodies (XPBD)
// Verify both solver paths complete and results merge correctly

// test_parallel_tick_empty_world
// Zero bodies — no jobs dispatched, no crash

// test_parallel_tick_single_body
// One body under gravity — correct result, no wasted jobs

// test_parallel_tick_tsan_clean
// 5000 bodies, 8 threads, TSan-instrumented — no data races
```
