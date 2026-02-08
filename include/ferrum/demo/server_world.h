#ifndef FERRUM_DEMO_SERVER_WORLD_H
#define FERRUM_DEMO_SERVER_WORLD_H

/** @file
 * @brief Demo server state: physics world + game-specific data.
 *
 * Manages ground plane, player bodies (kinematic), box spawning,
 * impulse beams, and random distant object spawning.
 *
 * Ownership: caller creates and destroys via init/destroy.
 * The struct owns all internal physics state.
 */

#include "ferrum/physics/world.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_cmd.h"
#include "ferrum/demo/input_move.h"
#include "ferrum/demo/input_spawn.h"
#include "ferrum/net/topic_channel.h"

#include <stdatomic.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEMO_MAX_CLIENTS     4
#define DEMO_MAX_BODIES    512
#define DEMO_GROUND_BODY     0   /**< Body index for the ground plane. */

/**
 * @brief Demo server world: physics world + game-specific tracking.
 *
 * Ownership: caller owns the struct; must call demo_server_world_destroy().
 * Nullability: all public functions are NULL-safe on the sw pointer.
 */
typedef struct demo_server_world {
    phys_world_t physics;                      /**< Physics world container. */

    /* Player tracking. */
    uint32_t player_body[DEMO_MAX_CLIENTS];    /**< Body index per client slot. */
    float    player_yaw[DEMO_MAX_CLIENTS];     /**< Yaw in radians per client. */
    float    player_pitch[DEMO_MAX_CLIENTS];   /**< Pitch in radians per client. */
    uint8_t  player_connected[DEMO_MAX_CLIENTS]; /**< 1 if slot occupied. */

    /* Spawning state. */
    uint32_t rng_state;           /**< xorshift32 RNG state. */
    uint32_t ticks_since_spawn;   /**< Ticks since last stack spawn. */
    uint32_t dynamic_body_count;  /**< Number of dynamic bodies spawned. */

    /* Stack spawn tracking. */
    float    stack_positions[64][2]; /**< XZ center of each spawned stack. */
    uint32_t stack_count;           /**< Number of stacks spawned so far. */

    /* Async physics tick state.
     * The tick runs as a fiber job.  tick_done is set atomically by the
     * job when complete.  The main thread polls it — never blocks. */
    atomic_int tick_done;           /**< Set to 1 when parallel tick completes. */
    uint8_t    tick_in_flight;      /**< 1 if a parallel tick is in progress. */

    /** Persistent storage for tick job arguments (must outlive the
     *  async job dispatch since demo_server_world_tick returns immediately). */
    struct {
        phys_world_t       *world;
        phys_job_context_t *jobs;
    } tick_args_;

    /** Command channel for deferred physics mutations.  Owned externally;
     *  the tick job drains it at the start of each step. */
    fr_topic_channel_t *cmd_channel;

    /* Body metadata for replication. */
    uint8_t  body_shape_type[DEMO_MAX_BODIES]; /**< 0=box, 1=sphere. */
    uint32_t body_color_seed[DEMO_MAX_BODIES]; /**< Color seed per body. */

    /** Per-client bitset tracking which bodies have been BODY_SPAWN'd.
     *  Indexed as body_known[client][byte], bit = body_index & 7. */
    uint8_t body_known[DEMO_MAX_CLIENTS][DEMO_MAX_BODIES / 8];

    /** Per-body half-extents in mm for replication (set by spawn callback). */
    uint16_t body_half_mm[DEMO_MAX_BODIES][3];
} demo_server_world_t;

/**
 * @brief Initialize server world with ground plane.
 *
 * @param sw        Server world to initialize. Must not be NULL.
 * @param rng_seed  RNG seed; 0 uses default seed (12345).
 * @return 0 on success, -1 on failure.
 *
 * Ownership: caller owns sw and must call demo_server_world_destroy().
 * Side effects: allocates memory for the physics world.
 */
int demo_server_world_init(demo_server_world_t *sw, uint32_t rng_seed);

/**
 * @brief Destroy server world and free all resources.
 *
 * @param sw  Server world to destroy. NULL-safe (no-op).
 *
 * Side effects: frees all internal physics memory.
 */
void demo_server_world_destroy(demo_server_world_t *sw);

/**
 * @brief Register a new player, creating a kinematic body.
 *
 * @param sw  Server world. Must not be NULL.
 * @return Client slot index (0 to DEMO_MAX_CLIENTS-1), or -1 if full.
 *
 * Side effects: creates a kinematic body in the physics world.
 */
int demo_server_world_add_player(demo_server_world_t *sw);

/**
 * @brief Remove a player and destroy their body.
 *
 * @param sw          Server world. Must not be NULL.
 * @param client_slot Slot index. Out-of-range or unoccupied is a no-op.
 *
 * Side effects: destroys the player's body in the physics world.
 */
void demo_server_world_remove_player(demo_server_world_t *sw, int client_slot);

/**
 * @brief Apply movement input from a client.
 *
 * Updates yaw/pitch from the input's snorm16 fields and moves the
 * player's kinematic body based on WASD flags and the computed
 * forward/right vectors.
 *
 * If the fire action is set, applies an impulse to the nearest
 * dynamic body in the player's forward direction.
 *
 * @param sw          Server world. Must not be NULL.
 * @param client_slot Client slot. Out-of-range or unoccupied is a no-op.
 * @param input       Movement input. Must not be NULL.
 * @param dt          Delta time in seconds.
 *
 * Side effects: mutates player body position and potentially other
 *               body velocities (fire impulse).
 */
void demo_server_world_apply_input(demo_server_world_t *sw, int client_slot,
                                    const demo_input_move_t *input, float dt);

/**
 * @brief Spawn a box requested by a client.
 *
 * Creates a dynamic body 2m in front of the player at eye height,
 * with mass proportional to volume (density 500 kg/m³).
 *
 * @param sw          Server world. Must not be NULL.
 * @param client_slot Client slot. Must be a connected player.
 * @param spawn       Spawn parameters. Must not be NULL.
 * @return Body index, or UINT32_MAX on failure.
 *
 * Side effects: creates a dynamic body with initial velocity.
 */
uint32_t demo_server_world_spawn_box(demo_server_world_t *sw, int client_slot,
                                      const demo_input_spawn_t *spawn);

/**
 * @brief Kick off the physics tick asynchronously.
 *
 * When @p jobs is non-NULL, dispatches phys_world_tick_parallel() as a
 * job and returns immediately.  When @p jobs is NULL, runs the tick
 * synchronously.
 *
 * Caller must ensure no tick is already in flight (poll with
 * demo_server_world_tick_done() and consume with
 * demo_server_world_tick_consume() first).
 *
 * Also runs the random distant object spawner each call.
 *
 * @param sw    Server world. Must not be NULL.
 * @param jobs  Physics job context for parallel dispatch (may be NULL).
 *
 * Side effects: advances physics, may create new bodies.
 */
void demo_server_world_tick(demo_server_world_t *sw, phys_job_context_t *jobs);

/**
 * @brief Poll whether the in-flight tick has completed.
 *
 * @param sw  Server world. NULL-safe (returns 1).
 * @return 1 if no tick is in flight or the tick has finished, 0 if still running.
 */
int demo_server_world_tick_done(const demo_server_world_t *sw);

/**
 * @brief Mark the in-flight tick as consumed after tick_done returns 1.
 *
 * Resets tick_in_flight so a new tick can be dispatched.
 *
 * @param sw  Server world. Must not be NULL.
 */
void demo_server_world_tick_consume(demo_server_world_t *sw);

/**
 * @brief Block until an in-flight tick completes (shutdown only).
 *
 * Spins on the atomic flag.  Only intended for clean shutdown, not
 * the hot path.  No-op if no tick is in flight.
 *
 * @param sw  Server world. NULL-safe.
 */
void demo_server_world_tick_wait(demo_server_world_t *sw);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_DEMO_SERVER_WORLD_H */
