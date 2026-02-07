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
#include "ferrum/demo/input_move.h"
#include "ferrum/demo/input_spawn.h"

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
    uint32_t ticks_since_spawn;   /**< Ticks since last random distant spawn. */
    uint32_t dynamic_body_count;  /**< Number of dynamic bodies spawned. */

    /* Body metadata for replication. */
    uint8_t  body_shape_type[DEMO_MAX_BODIES]; /**< 0=box, 1=sphere. */
    uint32_t body_color_seed[DEMO_MAX_BODIES]; /**< Color seed per body. */
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
 * @brief Tick physics and the random distant object spawner.
 *
 * Calls phys_world_tick, increments ticks_since_spawn, and spawns
 * a random distant body when the spawn interval elapses.
 *
 * @param sw  Server world. Must not be NULL.
 *
 * Side effects: advances physics, may create new bodies.
 */
void demo_server_world_tick(demo_server_world_t *sw);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_DEMO_SERVER_WORLD_H */
