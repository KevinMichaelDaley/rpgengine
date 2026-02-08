/**
 * @file server_world_spawn.c
 * @brief Box spawning and physics tick with box stack stress test spawner.
 *
 * Handles client-requested box spawns and periodic box stack spawning
 * for physics stacking stability stress tests.
 */

#include "ferrum/demo/server_world.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"

#include <math.h>

/** Spawn distance in front of the player (meters). */
#define SPAWN_DISTANCE 2.0f

/** Initial upward velocity for player-spawned boxes (m/s). */
#define SPAWN_VEL_UP 3.0f

/** Initial forward velocity for player-spawned boxes (m/s). */
#define SPAWN_VEL_FORWARD 5.0f

/** Density for player-spawned boxes in kg/m³. */
#define PLAYER_BOX_DENSITY 500.0f

/** Pi constant. */
#define PI_F 3.14159265358979323846f

/** Minimum ticks between stack spawns (3 seconds at 60Hz). */
#define STACK_SPAWN_MIN 180u

/** Range of random extra ticks above minimum for stack spawns. */
#define STACK_SPAWN_RANGE 121u  /* 180-300 ticks = 3-5 seconds */

/** Maximum radius from origin for stack placement (meters). */
#define STACK_SPAWN_RADIUS 30.0f

/** Minimum distance between stack centers (meters). */
#define STACK_MIN_SPACING 5.0f

/** Minimum boxes per stack. */
#define STACK_MIN_HEIGHT 8u

/** Maximum boxes per stack. */
#define STACK_MAX_HEIGHT 50u

/** Maximum number of tracked stack positions. */
#define STACK_MAX_COUNT 64u

/** Minimum box half-extent (meters). */
#define BOX_HALF_MIN 0.3f

/** Maximum box half-extent (meters). */
#define BOX_HALF_MAX 1.5f

/** Minimum density for stack boxes (kg/m³). */
#define DENSITY_MIN 200.0f

/** Maximum density for stack boxes (kg/m³). */
#define DENSITY_MAX 2000.0f

/** Impulse magnitude scale (multiplied by target mass). */
#define IMPULSE_MASS_SCALE_MIN 2.0f
#define IMPULSE_MASS_SCALE_MAX 5.0f

/**
 * @brief xorshift32 pseudo-random number generator.
 *
 * @param state  Mutable RNG state. Must not be 0.
 * @return Next pseudo-random 32-bit value.
 */
static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/**
 * @brief Generate a random float in [lo, hi] using the RNG.
 */
static float rand_float(uint32_t *state, float lo, float hi) {
    uint32_t r = xorshift32(state);
    float t = (float)(r & 0xFFFFu) / 65535.0f;
    return lo + t * (hi - lo);
}

/**
 * @brief Check if a candidate XZ position is at least min_dist from all
 *        existing stack positions.
 *
 * @return true if the position is valid (far enough from all stacks).
 */
static bool stack_position_valid(const demo_server_world_t *sw,
                                  float cx, float cz, float min_dist) {
    float min_dist_sq = min_dist * min_dist;
    for (uint32_t i = 0; i < sw->stack_count; i++) {
        float dx = cx - sw->stack_positions[i][0];
        float dz = cz - sw->stack_positions[i][1];
        if (dx * dx + dz * dz < min_dist_sq) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Spawn a stack of boxes with varying sizes and masses.
 *
 * Boxes are placed on top of each other starting from the ground plane,
 * with random half-extents and densities.  After construction, a random
 * impulse is applied to a randomly chosen box in the stack.
 */
static void spawn_box_stack(demo_server_world_t *sw) {
    if (sw->stack_count >= STACK_MAX_COUNT) {
        return;
    }

    /* Find a valid position: random angle + distance within STACK_SPAWN_RADIUS.
     * Try up to 16 candidates to find one with adequate spacing. */
    float cx = 0.0f, cz = 0.0f;
    bool found = false;
    for (int attempt = 0; attempt < 16; attempt++) {
        float angle = rand_float(&sw->rng_state, 0.0f, 2.0f * PI_F);
        float dist  = rand_float(&sw->rng_state, 3.0f, STACK_SPAWN_RADIUS);
        cx = cosf(angle) * dist;
        cz = sinf(angle) * dist;
        if (stack_position_valid(sw, cx, cz, STACK_MIN_SPACING)) {
            found = true;
            break;
        }
    }
    if (!found) {
        return; /* No valid position found; skip this spawn. */
    }

    /* Determine stack height (8-50 boxes). */
    uint32_t stack_height = STACK_MIN_HEIGHT +
        (xorshift32(&sw->rng_state) % (STACK_MAX_HEIGHT - STACK_MIN_HEIGHT + 1u));

    /* Cap to available body budget (leave room for player boxes). */
    uint32_t available = 0;
    if (sw->dynamic_body_count + 20u < DEMO_MAX_BODIES) {
        available = DEMO_MAX_BODIES - sw->dynamic_body_count - 20u;
    }
    if (stack_height > available) {
        stack_height = available;
    }
    if (stack_height < STACK_MIN_HEIGHT) {
        return; /* Not enough room for a meaningful stack. */
    }

    /* Choose a stack "personality" for size distribution:
     * 0 = uniform large, 1 = uniform small, 2 = mixed, 3 = inverted pyramid
     * (heavy/large on top, small on bottom). */
    uint32_t personality = xorshift32(&sw->rng_state) % 4u;

    /* Track body indices for the impulse step. */
    uint32_t first_body = UINT32_MAX;
    uint32_t body_count_in_stack = 0;
    float stack_y = 0.0f; /* Current top of the stack (starts at ground). */

    for (uint32_t i = 0; i < stack_height; i++) {
        uint32_t body_idx = phys_world_create_body(&sw->physics);
        if (body_idx == UINT32_MAX || body_idx >= DEMO_MAX_BODIES) {
            break;
        }

        if (first_body == UINT32_MAX) {
            first_body = body_idx;
        }

        /* Determine half-extents based on personality. */
        float half;
        switch (personality) {
        case 0: /* Uniform large */
            half = rand_float(&sw->rng_state, 0.8f, BOX_HALF_MAX);
            break;
        case 1: /* Uniform small */
            half = rand_float(&sw->rng_state, BOX_HALF_MIN, 0.6f);
            break;
        case 2: /* Mixed */
            half = rand_float(&sw->rng_state, BOX_HALF_MIN, BOX_HALF_MAX);
            break;
        case 3: /* Inverted pyramid: small at bottom, large on top */
            {
                float t = (float)i / (float)(stack_height > 1 ? stack_height - 1 : 1);
                float lo = BOX_HALF_MIN + t * (BOX_HALF_MAX - BOX_HALF_MIN) * 0.5f;
                float hi = lo + 0.4f;
                if (hi > BOX_HALF_MAX) hi = BOX_HALF_MAX;
                half = rand_float(&sw->rng_state, lo, hi);
            }
            break;
        default:
            half = rand_float(&sw->rng_state, BOX_HALF_MIN, BOX_HALF_MAX);
            break;
        }

        /* Slight random aspect ratio variation (±20% per axis). */
        float hx = half * rand_float(&sw->rng_state, 0.8f, 1.2f);
        float hy = half * rand_float(&sw->rng_state, 0.8f, 1.2f);
        float hz = half * rand_float(&sw->rng_state, 0.8f, 1.2f);
        phys_vec3_t half_ext = {hx, hy, hz};

        /* Random density for this box. */
        float density = rand_float(&sw->rng_state, DENSITY_MIN, DENSITY_MAX);
        float volume = (2.0f * hx) * (2.0f * hy) * (2.0f * hz);
        float mass = density * volume;
        if (mass < 0.1f) mass = 0.1f;

        /* Position: centered on stack XZ, bottom touching current stack top. */
        float body_y = stack_y + hy;

        phys_body_t *b = phys_world_get_body(&sw->physics, body_idx);
        if (!b) break;

        b->position.x = cx;
        b->position.y = body_y;
        b->position.z = cz;

        phys_body_set_mass(b, mass);
        phys_body_set_box_inertia(b, mass, half_ext);

        /* Copy to next buffer. */
        phys_body_t *b_next = phys_body_pool_get_next(&sw->physics.body_pool,
                                                       body_idx);
        if (b_next) *b_next = *b;

        /* Attach collider. */
        phys_world_set_box_collider(&sw->physics, body_idx, half_ext,
                                    (phys_vec3_t){0.0f, 0.0f, 0.0f},
                                    (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f});

        sw->body_shape_type[body_idx] = 0;
        sw->body_color_seed[body_idx] = xorshift32(&sw->rng_state);
        sw->dynamic_body_count++;
        body_count_in_stack++;

        /* Advance stack top by the full height of this box. */
        stack_y += 2.0f * hy;
    }

    /* Apply a random impulse to a random box in the stack. */
    if (body_count_in_stack > 0 && first_body != UINT32_MAX) {
        uint32_t target_offset = xorshift32(&sw->rng_state) % body_count_in_stack;
        uint32_t target_body = first_body + target_offset;

        phys_body_t *tb = phys_world_get_body(&sw->physics, target_body);
        if (tb && !phys_body_is_static(tb)) {
            /* Random 3D impulse direction. */
            float ix = rand_float(&sw->rng_state, -1.0f, 1.0f);
            float iy = rand_float(&sw->rng_state, -0.5f, 1.0f);
            float iz = rand_float(&sw->rng_state, -1.0f, 1.0f);
            float len = sqrtf(ix * ix + iy * iy + iz * iz);
            if (len > 0.001f) {
                ix /= len; iy /= len; iz /= len;
            }

            /* Scale impulse by target mass. */
            float scale = rand_float(&sw->rng_state,
                                     IMPULSE_MASS_SCALE_MIN,
                                     IMPULSE_MASS_SCALE_MAX);
            float impulse_mag = (1.0f / tb->inv_mass) * scale;
            tb->linear_vel.x += ix * impulse_mag * tb->inv_mass;
            tb->linear_vel.y += iy * impulse_mag * tb->inv_mass;
            tb->linear_vel.z += iz * impulse_mag * tb->inv_mass;

            /* Update next buffer too. */
            phys_body_t *tb_next = phys_body_pool_get_next(
                &sw->physics.body_pool, target_body);
            if (tb_next) *tb_next = *tb;
        }
    }

    /* Record stack position. */
    sw->stack_positions[sw->stack_count][0] = cx;
    sw->stack_positions[sw->stack_count][1] = cz;
    sw->stack_count++;
}

/* ── Public API (2 non-static functions) ───────────────────────── */

uint32_t demo_server_world_spawn_box(demo_server_world_t *sw, int client_slot,
                                      const demo_input_spawn_t *spawn) {
    if (!sw || !spawn || client_slot < 0 || client_slot >= DEMO_MAX_CLIENTS) {
        return UINT32_MAX;
    }
    if (!sw->player_connected[client_slot]) {
        return UINT32_MAX;
    }

    uint32_t body_idx = phys_world_create_body(&sw->physics);
    if (body_idx == UINT32_MAX || body_idx >= DEMO_MAX_BODIES) {
        return UINT32_MAX;
    }

    /* Convert half-extents from millimeters to meters. */
    float hx = (float)spawn->half_x_mm * 0.001f;
    float hy = (float)spawn->half_y_mm * 0.001f;
    float hz = (float)spawn->half_z_mm * 0.001f;
    phys_vec3_t half_ext = {hx, hy, hz};

    /* Compute mass from volume * density. */
    float volume = (2.0f * hx) * (2.0f * hy) * (2.0f * hz);
    float mass   = volume * PLAYER_BOX_DENSITY;
    if (mass < 0.01f) {
        mass = 0.01f; /* minimum mass */
    }

    /* Position: 2m in front of player at eye height. */
    phys_body_t *player = phys_world_get_body(&sw->physics,
                                               sw->player_body[client_slot]);
    if (!player) {
        phys_world_destroy_body(&sw->physics, body_idx);
        return UINT32_MAX;
    }

    float yaw = sw->player_yaw[client_slot];
    float fwd_x =  sinf(yaw);
    float fwd_z = -cosf(yaw);

    phys_body_t *b = phys_world_get_body(&sw->physics, body_idx);
    if (!b) {
        return UINT32_MAX;
    }

    b->position.x = player->position.x + fwd_x * SPAWN_DISTANCE;
    b->position.y = player->position.y + 0.9f; /* eye height offset */
    b->position.z = player->position.z + fwd_z * SPAWN_DISTANCE;

    /* Set mass and inertia. */
    phys_body_set_mass(b, mass);
    phys_body_set_box_inertia(b, mass, half_ext);

    /* Initial velocity: forward + upward. */
    b->linear_vel.x = fwd_x * SPAWN_VEL_FORWARD;
    b->linear_vel.y = SPAWN_VEL_UP;
    b->linear_vel.z = fwd_z * SPAWN_VEL_FORWARD;

    /* Copy to next buffer. */
    phys_body_t *b_next = phys_body_pool_get_next(&sw->physics.body_pool,
                                                   body_idx);
    if (b_next) {
        *b_next = *b;
    }

    /* Attach box collider. */
    phys_world_set_box_collider(&sw->physics, body_idx, half_ext,
                                (phys_vec3_t){0.0f, 0.0f, 0.0f},
                                (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f});

    /* Store replication metadata. */
    sw->body_shape_type[body_idx] = 0; /* box */
    sw->body_color_seed[body_idx] = spawn->color_seed;
    sw->dynamic_body_count++;

    return body_idx;
}

void demo_server_world_tick(demo_server_world_t *sw, phys_job_context_t *jobs) {
    if (!sw) {
        return;
    }

    if (jobs) {
        phys_world_tick_parallel(&sw->physics, NULL, jobs);
    } else {
        phys_world_tick(&sw->physics, NULL);
    }

    sw->ticks_since_spawn++;

    /* Spawn a new box stack every 3-5 seconds (180-300 ticks). */
    uint32_t threshold = STACK_SPAWN_MIN +
                         (xorshift32(&sw->rng_state) % STACK_SPAWN_RANGE);

    if (sw->ticks_since_spawn >= threshold) {
        spawn_box_stack(sw);
        sw->ticks_since_spawn = 0;
    }
}
