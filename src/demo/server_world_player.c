/**
 * @file server_world_player.c
 * @brief Player management and input handling for the demo server.
 *
 * Handles adding/removing kinematic player bodies and applying
 * movement + fire inputs.
 */

#include "ferrum/demo/server_world.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/phys_pool.h"

#include <math.h>
#include <string.h>

/** Player move speed in units per second. */
#define PLAYER_MOVE_SPEED 5.0f

/** Player capsule-like box half-extents (x=0.4, y=0.9, z=0.4). */
#define PLAYER_HALF_X 0.4f
#define PLAYER_HALF_Y 0.9f
#define PLAYER_HALF_Z 0.4f

/** Fire impulse magnitude applied to targeted body. */
#define FIRE_IMPULSE 20.0f

/** Maximum distance for fire ray check. */
#define FIRE_MAX_DIST 50.0f

/** Pi constant for angle conversion. */
#define PI_F 3.14159265358979323846f

/**
 * @brief Convert snorm16 value to radians.
 *
 * Maps [-32767, 32767] to [-pi, pi].
 */
static float snorm16_to_radians(int16_t val) {
    return ((float)val / 32767.0f) * PI_F;
}

/**
 * @brief Compute forward direction vector from yaw and pitch.
 *
 * yaw = 0 points along -Z (standard FPS convention).
 */
static phys_vec3_t forward_from_yaw_pitch(float yaw, float pitch) {
    phys_vec3_t fwd;
    fwd.x = sinf(yaw) * cosf(pitch);
    fwd.y = sinf(pitch);
    fwd.z = -cosf(yaw) * cosf(pitch);
    return fwd;
}

/**
 * @brief Compute right direction vector from yaw (horizontal only).
 */
static phys_vec3_t right_from_yaw(float yaw) {
    phys_vec3_t right;
    right.x = cosf(yaw);
    right.y = 0.0f;
    right.z = sinf(yaw);
    return right;
}

/* ── Public API (3 non-static functions) ───────────────────────── */

int demo_server_world_add_player(demo_server_world_t *sw) {
    if (!sw) {
        return -1;
    }

    /* Find first empty slot. */
    int slot = -1;
    for (int i = 0; i < DEMO_MAX_CLIENTS; i++) {
        if (!sw->player_connected[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -1;
    }

    /* Create a kinematic body for the player. */
    uint32_t body_idx = phys_world_create_body(&sw->physics);
    if (body_idx == UINT32_MAX) {
        return -1;
    }

    phys_body_t *pb = phys_world_get_body(&sw->physics, body_idx);
    pb->position = (phys_vec3_t){0.0f, 1.0f, -5.0f * (float)slot};
    pb->flags    = PHYS_BODY_FLAG_KINEMATIC;
    pb->inv_mass = 0.0f;

    /* Copy to next buffer for consistent double-buffered state. */
    phys_body_t *pb_next = phys_body_pool_get_next(&sw->physics.body_pool,
                                                    body_idx);
    *pb_next = *pb;

    /* Attach a box collider representing the player capsule. */
    phys_world_set_box_collider(&sw->physics, body_idx,
                                (phys_vec3_t){PLAYER_HALF_X, PLAYER_HALF_Y,
                                              PLAYER_HALF_Z},
                                (phys_vec3_t){0.0f, 0.0f, 0.0f},
                                (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f});

    sw->player_body[slot]      = body_idx;
    sw->player_yaw[slot]       = 0.0f;
    sw->player_pitch[slot]     = 0.0f;
    sw->player_connected[slot] = 1;

    return slot;
}

void demo_server_world_remove_player(demo_server_world_t *sw, int client_slot) {
    if (!sw || client_slot < 0 || client_slot >= DEMO_MAX_CLIENTS) {
        return;
    }
    if (!sw->player_connected[client_slot]) {
        return;
    }

    phys_world_destroy_body(&sw->physics, sw->player_body[client_slot]);
    sw->player_body[client_slot]      = UINT32_MAX;
    sw->player_connected[client_slot] = 0;
    sw->player_yaw[client_slot]       = 0.0f;
    sw->player_pitch[client_slot]     = 0.0f;
}

void demo_server_world_apply_input(demo_server_world_t *sw, int client_slot,
                                    const demo_input_move_t *input, float dt) {
    if (!sw || !input || client_slot < 0 || client_slot >= DEMO_MAX_CLIENTS) {
        return;
    }
    if (!sw->player_connected[client_slot]) {
        return;
    }

    /* Update look angles from quantized input. */
    sw->player_yaw[client_slot]   = snorm16_to_radians(input->yaw_snorm16);
    sw->player_pitch[client_slot] = snorm16_to_radians(input->pitch_snorm16);

    float yaw   = sw->player_yaw[client_slot];
    float pitch = sw->player_pitch[client_slot];

    /* Compute movement basis vectors. */
    phys_vec3_t fwd_flat;
    fwd_flat.x =  sinf(yaw);
    fwd_flat.y =  0.0f;
    fwd_flat.z = -cosf(yaw);

    phys_vec3_t right = right_from_yaw(yaw);

    /* Accumulate movement direction from WASD flags. */
    phys_vec3_t move = {0.0f, 0.0f, 0.0f};
    if (input->move_flags & DEMO_MOVE_W) {
        move.x += fwd_flat.x;
        move.z += fwd_flat.z;
    }
    if (input->move_flags & DEMO_MOVE_S) {
        move.x -= fwd_flat.x;
        move.z -= fwd_flat.z;
    }
    if (input->move_flags & DEMO_MOVE_D) {
        move.x += right.x;
        move.z += right.z;
    }
    if (input->move_flags & DEMO_MOVE_A) {
        move.x -= right.x;
        move.z -= right.z;
    }

    /* Normalize and scale by speed * dt. */
    float move_len = sqrtf(move.x * move.x + move.z * move.z);
    if (move_len > 1e-6f) {
        float scale = PLAYER_MOVE_SPEED * dt / move_len;
        move.x *= scale;
        move.z *= scale;
    }

    /* Apply position delta to the kinematic player body. */
    phys_body_t *pb = phys_world_get_body(&sw->physics,
                                           sw->player_body[client_slot]);
    if (!pb) {
        return;
    }
    pb->position.x += move.x;
    pb->position.z += move.z;

    /* Sync to next buffer so tick sees updated position. */
    phys_body_t *pb_next = phys_body_pool_get_next(
        &sw->physics.body_pool, sw->player_body[client_slot]);
    if (pb_next) {
        *pb_next = *pb;
    }

    /* Handle fire action: apply impulse to nearest body in forward direction. */
    if (input->action_flags & DEMO_ACTION_FIRE) {
        phys_vec3_t fwd_3d = forward_from_yaw_pitch(yaw, pitch);
        phys_vec3_t eye    = pb->position;
        eye.y += PLAYER_HALF_Y; /* eye level */

        float best_dist  = FIRE_MAX_DIST;
        uint32_t best_idx = UINT32_MAX;

        /* Brute-force search for nearest body along forward ray. */
        uint32_t capacity = sw->physics.body_pool.capacity;
        for (uint32_t i = 0; i < capacity; i++) {
            if (!phys_body_pool_is_active(&sw->physics.body_pool, i)) {
                continue;
            }
            /* Skip ground and own body. */
            if (i == DEMO_GROUND_BODY ||
                i == sw->player_body[client_slot]) {
                continue;
            }

            phys_body_t *target = phys_world_get_body(&sw->physics, i);
            if (!target || phys_body_is_static(target) ||
                phys_body_is_kinematic(target)) {
                continue;
            }

            /* Vector from eye to target center. */
            phys_vec3_t to_target;
            to_target.x = target->position.x - eye.x;
            to_target.y = target->position.y - eye.y;
            to_target.z = target->position.z - eye.z;

            /* Project onto forward direction. */
            float proj = to_target.x * fwd_3d.x +
                         to_target.y * fwd_3d.y +
                         to_target.z * fwd_3d.z;
            if (proj <= 0.0f || proj >= best_dist) {
                continue;
            }

            /* Perpendicular distance from the ray. */
            phys_vec3_t on_ray;
            on_ray.x = eye.x + fwd_3d.x * proj;
            on_ray.y = eye.y + fwd_3d.y * proj;
            on_ray.z = eye.z + fwd_3d.z * proj;

            float perp_dx = target->position.x - on_ray.x;
            float perp_dy = target->position.y - on_ray.y;
            float perp_dz = target->position.z - on_ray.z;
            float perp_dist = sqrtf(perp_dx * perp_dx +
                                    perp_dy * perp_dy +
                                    perp_dz * perp_dz);

            /* Accept if within ~2m lateral tolerance. */
            if (perp_dist < 2.0f) {
                best_dist = proj;
                best_idx  = i;
            }
        }

        /* Apply impulse to the best target. */
        if (best_idx != UINT32_MAX) {
            phys_body_t *hit = phys_world_get_body(&sw->physics, best_idx);
            if (hit && hit->inv_mass > 0.0f) {
                float impulse_scale = FIRE_IMPULSE * hit->inv_mass;
                hit->linear_vel.x += fwd_3d.x * impulse_scale;
                hit->linear_vel.y += fwd_3d.y * impulse_scale;
                hit->linear_vel.z += fwd_3d.z * impulse_scale;

                /* Sync to next buffer. */
                phys_body_t *hit_next = phys_body_pool_get_next(
                    &sw->physics.body_pool, best_idx);
                if (hit_next) {
                    *hit_next = *hit;
                }
            }
        }
    }
}
