#ifndef FERRUM_SERVER_PLAYER_CONNECTION_H
#define FERRUM_SERVER_PLAYER_CONNECTION_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/math/vec3.h"

/** \file
 * \brief Player connectivity model used by server simulation + networking.
 *
 * This is intentionally separate from “entity” state:
 * - Many entities (NPCs, props, items, projectiles) can spawn without any player join.
 * - Some players may join but should not spawn to all remote clients (interest/visibility).
 *
 * All fields are plain data (no external references).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Plain-data record describing a connected player.
 *
 * Ownership:
 * - Owned by the server simulation layer.
 * - Copied/updated explicitly; no hidden global state.
 */
typedef struct player_connection {
    /** Stable id for the connected player (server-chosen). */
    uint16_t player_id;

    /** World-space position (meters), stored by value. */
    vec3_t world_pos;

    /** Whether this player's in-world representation should be spawned to remote clients.
     * Used by interest/visibility logic.
     */
    bool player_should_spawn_remote;
} player_connection_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_SERVER_PLAYER_CONNECTION_H */
