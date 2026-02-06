#ifndef FERRUM_PHYSICS_GAME_STATE_H
#define FERRUM_PHYSICS_GAME_STATE_H

/** @file
 * @brief Game state input structure for physics tier classification.
 *
 * Defines the input structure that provides gameplay context to the physics
 * system each tick. Includes player positions (for tier classification),
 * camera position (for LOD), and manipulation flags (for T0 promotion).
 *
 * The physics system reads this but does not own it — it is provided by
 * gameplay each tick. All functions are NULL-safe.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/memory/pool.h"
#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of players tracked by the physics system. */
#define PHYS_MAX_PLAYERS 16

/** Maximum number of hazard body indices. */
#define PHYS_MAX_HAZARDS 64

/* ── Player state ───────────────────────────────────────────────── */

/**
 * @brief Player state for physics tier classification.
 *
 * Contains per-player data needed for proximity-based LOD and
 * manipulation tracking. Manipulation fields are embedded directly
 * rather than using a separate type to satisfy the 2-type header rule.
 *
 * Ownership: value type, no heap allocation.
 */
typedef struct phys_player_state {
    phys_vec3_t position;          /**< World-space player position. */
    phys_vec3_t velocity;          /**< Current velocity vector. */
    phys_vec3_t look_direction;    /**< Direction the player is facing. */
    float interaction_radius;      /**< How far the player can reach. */

    /* Manipulation state (embedded). */
    pool_handle_t manipulation_body; /**< Handle of body being manipulated. */
    uint8_t manipulation_type;       /**< 0=none, 1=grab, 2=push, 3=carry. */
    bool has_manipulation;           /**< True if player is manipulating a body. */
    uint8_t pad[2];                  /**< Padding for alignment. */
} phys_player_state_t;

/* ── Game state ─────────────────────────────────────────────────── */

/**
 * @brief Game state input provided by gameplay each tick.
 *
 * Aggregates player data, camera information, hazard hints, and
 * time-scaling for use by physics stages (tier classification, LOD).
 *
 * Ownership: value type with fixed-size arrays, no heap allocation.
 * Nullability: all query/mutation functions accept NULL state safely.
 */
typedef struct phys_game_state {
    /* Player data. */
    phys_player_state_t players[PHYS_MAX_PLAYERS]; /**< Per-player state. */
    uint32_t player_count;                          /**< Active player count. */

    /* Camera (for LOD/view-frustum decisions). */
    phys_vec3_t camera_position;  /**< Camera world position. */
    phys_vec3_t camera_forward;   /**< Camera forward direction (unit). */
    float camera_fov_rad;         /**< Camera field of view in radians. */

    /* Gameplay hints. */
    uint32_t hazard_indices[PHYS_MAX_HAZARDS]; /**< Body indices marked hazardous. */
    uint32_t hazard_count;                      /**< Current number of hazard entries. */

    /* Time. */
    float game_time;   /**< Current game time in seconds. */
    float time_scale;  /**< Time multiplier (1.0 = normal, <1 = slow-mo). */
} phys_game_state_t;

/* ── Core API (game_state_core.c) ───────────────────────────────── */

/**
 * @brief Initialize a game state to safe defaults.
 *
 * Zeroes all fields and sets time_scale to 1.0f.
 *
 * @param state Game state to initialize (if NULL, no-op).
 * @return No return value.
 * @note No side effects beyond writing *state.
 */
void phys_game_state_init(phys_game_state_t *state);

/**
 * @brief Set player state at a given index.
 *
 * Copies @p player into state->players[index]. Updates player_count
 * to max(player_count, index + 1).
 *
 * @param state  Game state (if NULL, no-op).
 * @param index  Player slot index (must be < PHYS_MAX_PLAYERS or no-op).
 * @param player Player data to copy (if NULL, no-op).
 * @return No return value.
 * @note No side effects beyond writing *state.
 */
void phys_game_state_set_player(phys_game_state_t *state, uint32_t index,
                                const phys_player_state_t *player);

/**
 * @brief Set camera parameters.
 *
 * @param state    Game state (if NULL, no-op).
 * @param position Camera world position.
 * @param forward  Camera forward direction (should be unit length).
 * @param fov      Field of view in radians.
 * @return No return value.
 * @note No side effects beyond writing *state.
 */
void phys_game_state_set_camera(phys_game_state_t *state,
                                phys_vec3_t position, phys_vec3_t forward,
                                float fov);

/**
 * @brief Add a hazard body index.
 *
 * Appends @p body_index to the hazard array if not full.
 * Silently ignored when hazard_count >= PHYS_MAX_HAZARDS.
 *
 * @param state      Game state (if NULL, no-op).
 * @param body_index Index of the hazardous body.
 * @return No return value.
 * @note No side effects beyond writing *state.
 */
void phys_game_state_add_hazard(phys_game_state_t *state, uint32_t body_index);

/* ── Query API (game_state_query.c) ─────────────────────────────── */

/**
 * @brief Check if a body is currently being manipulated by any player.
 *
 * Iterates active players and checks for matching pool handle
 * (both index and generation must match).
 *
 * @param state Game state (if NULL, returns false).
 * @param body  Pool handle to check.
 * @return true if any player is manipulating that body, false otherwise.
 * @note O(player_count) scan.
 */
bool phys_game_state_is_manipulated(const phys_game_state_t *state,
                                    pool_handle_t body);

/**
 * @brief Compute distance to the nearest active player.
 *
 * @param state Game state (if NULL, returns FLT_MAX).
 * @param pos   World position to measure from.
 * @return Distance to nearest player, or FLT_MAX if no players.
 * @note O(player_count) scan.
 */
float phys_game_state_distance_to_nearest_player(const phys_game_state_t *state,
                                                 phys_vec3_t pos);

/**
 * @brief Check if a position is within the camera's field of view.
 *
 * Uses a cone test: computes the angle between camera forward and the
 * direction to @p pos, then compares against half-FOV plus an angular
 * offset based on @p radius.
 *
 * @param state  Game state (if NULL, returns false).
 * @param pos    World position to test.
 * @param radius Object bounding radius (expands the effective cone).
 * @return true if any part of the sphere (pos, radius) is in the FOV cone.
 */
bool phys_game_state_is_in_view(const phys_game_state_t *state,
                                phys_vec3_t pos, float radius);

/**
 * @brief Clear all hazard entries.
 *
 * Sets hazard_count to zero without modifying the underlying buffer.
 *
 * @param state Game state (if NULL, no-op).
 * @return No return value.
 * @note No side effects beyond writing *state.
 */
void phys_game_state_clear_hazards(phys_game_state_t *state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_GAME_STATE_H */
