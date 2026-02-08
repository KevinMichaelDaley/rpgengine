/**
 * @file server_world_spawn.c
 * @brief Box spawning and physics tick with box stack stress test spawner.
 *
 * Handles client-requested box spawns and periodic box stack spawning
 * for physics stacking stability stress tests.
 */

#define _POSIX_C_SOURCE 200809L

#include "ferrum/demo/server_world.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"

#include <math.h>
#include <string.h>
#include <time.h>

/** Return wall-clock milliseconds (CLOCK_MONOTONIC). */
static uint64_t spawn_now_ms_(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

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

/** Minimum milliseconds between stack spawns. */
#define STACK_SPAWN_MIN_MS 3000u

/** Range of random extra milliseconds above minimum for stack spawns. */
#define STACK_SPAWN_RANGE_MS 2000u  /* 3-5 seconds */

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
    if (sw->stack_count >= STACK_MAX_COUNT || !sw->cmd_channel) {
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

    /* Pick which box in the stack receives the random impulse (baked
     * into its initial linear_vel since we don't know body indices). */
    uint32_t impulse_target = xorshift32(&sw->rng_state) % stack_height;

    /* Pre-compute the impulse velocity for the target box.
     * impulse_mag = mass * scale, applied as vel += impulse_mag * inv_mass
     * = scale.  So the velocity boost is just the scaled direction. */
    float dir_x = rand_float(&sw->rng_state, -1.0f, 1.0f);
    float dir_y = rand_float(&sw->rng_state, -0.5f, 1.0f);
    float dir_z = rand_float(&sw->rng_state, -1.0f, 1.0f);
    float dir_len = sqrtf(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
    if (dir_len > 0.001f) {
        dir_x /= dir_len;
        dir_y /= dir_len;
        dir_z /= dir_len;
    }
    float impulse_scale = rand_float(&sw->rng_state,
                                     IMPULSE_MASS_SCALE_MIN,
                                     IMPULSE_MASS_SCALE_MAX);

    float stack_y = 0.0f; /* Current top of the stack (starts at ground). */

    for (uint32_t i = 0; i < stack_height; i++) {
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

        /* Random density for this box. */
        float density = rand_float(&sw->rng_state, DENSITY_MIN, DENSITY_MAX);
        float volume = (2.0f * hx) * (2.0f * hy) * (2.0f * hz);
        float mass = density * volume;
        if (mass < 0.1f) mass = 0.1f;

        /* Build spawn command. */
        phys_cmd_spawn_body_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.position.x  = cx;
        cmd.position.y  = stack_y + hy;
        cmd.position.z  = cz;
        cmd.orientation  = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};
        cmd.mass         = mass;
        cmd.flags        = 0;
        cmd.shape        = PHYS_CMD_SHAPE_BOX;
        cmd.shape_data.box_half = (phys_vec3_t){hx, hy, hz};

        /* Bake impulse velocity into the target box's initial velocity. */
        if (i == impulse_target) {
            cmd.linear_vel.x = dir_x * impulse_scale;
            cmd.linear_vel.y = dir_y * impulse_scale;
            cmd.linear_vel.z = dir_z * impulse_scale;
        }

        /* Encode demo metadata: low 8 bits = shape (0=box),
         * bits 8..39 = color seed. */
        uint32_t color = xorshift32(&sw->rng_state);
        cmd.user_tag = (uint64_t)color << 8u;

        phys_cmd_push(sw->cmd_channel, PHYS_CMD_SPAWN_BODY,
                      &cmd, sizeof(cmd));

        /* Advance stack top by the full height of this box. */
        stack_y += 2.0f * hy;
    }

    /* Record stack position. */
    sw->stack_positions[sw->stack_count][0] = cx;
    sw->stack_positions[sw->stack_count][1] = cz;
    sw->stack_count++;
}

/* ── Public API (2 non-static functions) ───────────────────────── */

uint32_t demo_server_world_spawn_box(demo_server_world_t *sw, int client_slot,
                                      const demo_input_spawn_t *spawn) {
    if (!sw || !spawn || !sw->cmd_channel ||
        client_slot < 0 || client_slot >= DEMO_MAX_CLIENTS) {
        return UINT32_MAX;
    }
    if (!sw->player_connected[client_slot]) {
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
        mass = 0.01f;
    }

    /* Read player position from last completed tick (safe — bodies_curr
     * is stable while the tick writes to bodies_next). */
    const phys_body_t *player = phys_world_get_body(&sw->physics,
                                                     sw->player_body[client_slot]);
    if (!player) {
        return UINT32_MAX;
    }

    float yaw   = sw->player_yaw[client_slot];
    float fwd_x =  sinf(yaw);
    float fwd_z = -cosf(yaw);

    /* Build spawn command. */
    phys_cmd_spawn_body_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.position.x  = player->position.x + fwd_x * SPAWN_DISTANCE;
    cmd.position.y  = player->position.y + 0.9f;
    cmd.position.z  = player->position.z + fwd_z * SPAWN_DISTANCE;
    cmd.orientation  = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    cmd.linear_vel.x = fwd_x * SPAWN_VEL_FORWARD;
    cmd.linear_vel.y = SPAWN_VEL_UP;
    cmd.linear_vel.z = fwd_z * SPAWN_VEL_FORWARD;
    cmd.mass         = mass;
    cmd.flags        = 0;
    cmd.shape        = PHYS_CMD_SHAPE_BOX;
    cmd.shape_data.box_half = half_ext;

    /* Encode demo metadata in user_tag: low 8 = shape, bits 8..39 = color. */
    cmd.user_tag = (uint64_t)0u | ((uint64_t)spawn->color_seed << 8u);

    if (!phys_cmd_push(sw->cmd_channel, PHYS_CMD_SPAWN_BODY,
                       &cmd, sizeof(cmd))) {
        return UINT32_MAX;
    }

    /* Body index is assigned asynchronously by the tick job; return 0
     * as a placeholder (the caller only uses this for success/fail). */
    return 0u;
}


/* ── Spawn callback + tick wrappers ────────────────────────────────
 *
 * The actual async dispatch lives in phys_tick_runner.  These wrappers
 * add demo-specific spawn metadata recording and stack spawning.
 */

/** Tag type encoded in bits 63..56 of user_tag. */
#define TAG_TYPE_DYNAMIC  0u  /**< Dynamic body (box/sphere). */
#define TAG_TYPE_PLAYER   1u  /**< Player kinematic body. */

/** Extract tag type from user_tag. */
static uint8_t tag_get_type(uint64_t tag) {
    return (uint8_t)((tag >> 56u) & 0xFFu);
}

/** Callback for phys_cmd_drain: records demo metadata for spawned bodies. */
static void spawn_callback_(uint32_t body_index, uint64_t user_tag, void *user) {
    demo_server_world_t *sw = (demo_server_world_t *)user;
    if (body_index == UINT32_MAX || body_index >= DEMO_MAX_BODIES) {
        return;
    }

    uint8_t tag_type = tag_get_type(user_tag);

    if (tag_type == TAG_TYPE_PLAYER) {
        /* Bits 7..0 = client slot index. */
        uint8_t slot = (uint8_t)(user_tag & 0xFFu);
        if (slot < DEMO_MAX_CLIENTS) {
            sw->player_body[slot] = body_index;
        }
    } else {
        /* Dynamic body: bits 7..0 = shape type, bits 8..39 = color seed. */
        sw->body_shape_type[body_index] = (uint8_t)(user_tag & 0xFFu);
        sw->body_color_seed[body_index] =
            (uint32_t)((user_tag >> 8u) & 0xFFFFFFFFu);
        sw->dynamic_body_count++;
    }
}

void demo_server_world_tick_wait(demo_server_world_t *sw) {
    if (!sw) { return; }
    phys_tick_runner_wait(&sw->tick_runner);
}

int demo_server_world_tick_done(const demo_server_world_t *sw) {
    if (!sw) { return 1; }
    return phys_tick_runner_done(&sw->tick_runner);
}

void demo_server_world_tick_consume(demo_server_world_t *sw) {
    if (!sw) { return; }
    phys_tick_runner_consume(&sw->tick_runner);
}

void demo_server_world_tick(demo_server_world_t *sw, phys_job_context_t *jobs) {
    if (!sw) {
        return;
    }

    /* Spawn new stacks on a wall-clock timer (independent of call rate).
     * Commands go through the channel so the physics fiber picks them up. */
    {
        uint64_t now = spawn_now_ms_();
        if (sw->next_spawn_ms == 0u) {
            /* First call — schedule initial spawn 3-5s from now. */
            uint32_t delay = STACK_SPAWN_MIN_MS +
                             (xorshift32(&sw->rng_state) % STACK_SPAWN_RANGE_MS);
            sw->next_spawn_ms = now + delay;
        }
        if (now >= sw->next_spawn_ms) {
            spawn_box_stack(sw);
            uint32_t delay = STACK_SPAWN_MIN_MS +
                             (xorshift32(&sw->rng_state) % STACK_SPAWN_RANGE_MS);
            sw->next_spawn_ms = now + delay;
        }
    }

    if (jobs) {
        /* Ensure the runner is wired up (first tick may need this). */
        if (!sw->tick_runner.world) {
            phys_tick_runner_init(&sw->tick_runner, &sw->physics, jobs,
                                 sw->cmd_channel, NULL,
                                 spawn_callback_, sw);
        }
        phys_tick_runner_kick(&sw->tick_runner);
    } else {
        phys_world_tick(&sw->physics, NULL);
    }
}
