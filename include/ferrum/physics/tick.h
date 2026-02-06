#ifndef FERRUM_PHYSICS_TICK_H
#define FERRUM_PHYSICS_TICK_H

/** @file
 * @brief Master tick function for advancing the physics simulation one frame.
 *
 * Orchestrates all 14 pipeline stages in order with substep loop and
 * buffer swap.  This is the single entry point for advancing the
 * physics simulation.
 */

struct phys_world;
struct phys_game_state;
struct phys_job_context;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Advance the physics simulation by one frame.
 *
 * Runs all 14 pipeline stages with substep loop.  Uses the world's
 * frame arena for per-frame allocations (reset at start of tick).
 *
 * @param world  Physics world container.  NULL-safe (no-op).
 * @param game   Game state input (player state, etc).  May be NULL.
 *
 * @note Ownership: borrows world and game; does not take ownership.
 * @note Side effects: mutates world state (body positions, velocities,
 *       manifold cache, impact events, tick count).
 * @note Error semantics: NULL world is a silent no-op.
 */
void phys_world_tick(struct phys_world *world,
                     const struct phys_game_state *game);

/**
 * @brief Parallel tick — dispatches pipeline stages as jobs.
 *
 * Produces identical results to phys_world_tick() but uses the job system
 * for parallelism.  Sync stages (step plan, island build, cache commit)
 * run on the calling thread.  TGS and XPBD solve concurrently.
 *
 * @param world  Physics world (non-NULL).
 * @param game   Game state input (may be NULL).
 * @param jobs   Physics job context (non-NULL).
 */
void phys_world_tick_parallel(struct phys_world *world,
                              const struct phys_game_state *game,
                              struct phys_job_context *jobs);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_TICK_H */
