/**
 * @file game_state_query.c
 * @brief Game state query and utility functions.
 *
 * Provides is_manipulated, distance_to_nearest_player, is_in_view,
 * and clear_hazards operations. All functions are NULL-safe.
 */

#include "ferrum/physics/game_state.h"

#include <float.h>
#include <math.h>

#include "ferrum/math/constants.h"
#include "ferrum/math/vec3.h"

/* ── phys_game_state_is_manipulated ─────────────────────────────── */

bool phys_game_state_is_manipulated(const phys_game_state_t *state,
                                    pool_handle_t body) {
    if (!state) { return false; }
    for (uint32_t i = 0; i < state->player_count; i++) {
        const phys_player_state_t *p = &state->players[i];
        if (p->has_manipulation &&
            p->manipulation_body.index == body.index &&
            p->manipulation_body.generation == body.generation) {
            return true;
        }
    }
    return false;
}

/* ── phys_game_state_distance_to_nearest_player ─────────────────── */

float phys_game_state_distance_to_nearest_player(const phys_game_state_t *state,
                                                 phys_vec3_t pos) {
    if (!state || state->player_count == 0) { return FLT_MAX; }

    float min_dist = FLT_MAX;
    for (uint32_t i = 0; i < state->player_count; i++) {
        vec3_t diff = vec3_sub(pos, state->players[i].position);
        float dist = vec3_magnitude(diff);
        if (dist < min_dist) {
            min_dist = dist;
        }
    }
    return min_dist;
}

/* ── phys_game_state_is_in_view ─────────────────────────────────── */

bool phys_game_state_is_in_view(const phys_game_state_t *state,
                                phys_vec3_t pos, float radius) {
    if (!state) { return false; }

    vec3_t direction = vec3_sub(pos, state->camera_position);
    float dist = vec3_magnitude(direction);

    /* Object at camera position is always in view. */
    if (dist < 1e-4f) { return true; }

    /* Normalize direction and compute angle with camera forward. */
    vec3_t dir_norm = vec3_scale(direction, 1.0f / dist);
    float cos_angle = vec3_dot(dir_norm, state->camera_forward);

    float half_fov = state->camera_fov_rad * 0.5f;

    /* Expand the FOV cone by the angular radius of the object. */
    float angular_radius = (dist > radius)
                               ? atanf(radius / dist)
                               : (float)FERRUM_PI;

    return cos_angle > cosf(half_fov + angular_radius);
}

/* ── phys_game_state_clear_hazards ──────────────────────────────── */

void phys_game_state_clear_hazards(phys_game_state_t *state) {
    if (!state) { return; }
    state->hazard_count = 0;
}
