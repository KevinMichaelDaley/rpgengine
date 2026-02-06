/**
 * @file game_state_core.c
 * @brief Core game state mutation functions.
 *
 * Provides init, set_player, set_camera, and add_hazard operations.
 * All functions are NULL-safe.
 */

#include "ferrum/physics/game_state.h"

#include <string.h>

/* ── phys_game_state_init ───────────────────────────────────────── */

void phys_game_state_init(phys_game_state_t *state) {
    if (!state) { return; }
    memset(state, 0, sizeof(*state));
    state->time_scale = 1.0f;
}

/* ── phys_game_state_set_player ─────────────────────────────────── */

void phys_game_state_set_player(phys_game_state_t *state, uint32_t index,
                                const phys_player_state_t *player) {
    if (!state || !player) { return; }
    if (index >= PHYS_MAX_PLAYERS) { return; }

    state->players[index] = *player;

    /* Expand player_count to cover this slot. */
    uint32_t required = index + 1;
    if (required > state->player_count) {
        state->player_count = required;
    }
}

/* ── phys_game_state_set_camera ─────────────────────────────────── */

void phys_game_state_set_camera(phys_game_state_t *state,
                                phys_vec3_t position, phys_vec3_t forward,
                                float fov) {
    if (!state) { return; }
    state->camera_position = position;
    state->camera_forward = forward;
    state->camera_fov_rad = fov;
}

/* ── phys_game_state_add_hazard ─────────────────────────────────── */

void phys_game_state_add_hazard(phys_game_state_t *state, uint32_t body_index) {
    if (!state) { return; }
    if (state->hazard_count >= PHYS_MAX_HAZARDS) { return; }
    state->hazard_indices[state->hazard_count] = body_index;
    state->hazard_count++;
}
