#ifndef FERRUM_PHYSICS_TICK_H
#define FERRUM_PHYSICS_TICK_H

/** @file
 * @brief Master tick function for advancing the physics simulation one frame.
 *
 * Orchestrates all pipeline stages in order with substep loop and
 * buffer swap.  Uses the job system for parallelism.  This is the
 * single entry point for advancing the physics simulation.
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
 * Dispatches pipeline stages as jobs for parallelism.  Sync stages
 * (step plan, island build, cache commit) run on the calling fiber.
 * TGS and XPBD solve concurrently on separate fibers.
 *
 * @param world  Physics world container.  NULL-safe (no-op).
 * @param game   Game state input (player state, etc).  May be NULL.
 * @param jobs   Physics job context.  Must be non-NULL with a running
 *               job system.
 *
 * @note Ownership: borrows world and game; does not take ownership.
 * @note Side effects: mutates world state (body positions, velocities,
 *       manifold cache, impact events, tick count).
 * @note Error semantics: NULL world is a silent no-op.
 */
void phys_world_tick_parallel(struct phys_world *world,
                              const struct phys_game_state *game,
                              struct phys_job_context *jobs);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_TICK_H */
