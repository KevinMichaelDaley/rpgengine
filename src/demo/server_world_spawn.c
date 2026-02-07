/**
 * @file server_world_spawn.c
 * @brief Box spawning and physics tick with random distant object spawner.
 *
 * Handles client-requested box spawns and periodic random distant
 * body spawning during the tick loop.
 */

#include "ferrum/demo/server_world.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/phys_pool.h"

#include <math.h>

/** Density for spawned boxes in kg/m³. */
#define BOX_DENSITY 500.0f

/** Initial upward velocity for spawned boxes (m/s). */
#define SPAWN_VEL_UP 3.0f

/** Initial forward velocity for spawned boxes (m/s). */
#define SPAWN_VEL_FORWARD 5.0f

/** Spawn distance in front of the player (meters). */
#define SPAWN_DISTANCE 2.0f

/** Pi constant. */
#define PI_F 3.14159265358979323846f

/** Minimum ticks between random distant spawns. */
#define DISTANT_SPAWN_MIN 30u

/** Range of random extra ticks above minimum for distant spawns. */
#define DISTANT_SPAWN_RANGE 31u

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
 * @brief Spawn a random distant body (box or sphere) far from the origin.
 *
 * Called by the tick function when the spawn interval elapses.
 */
static void spawn_distant_object(demo_server_world_t *sw) {
    uint32_t body_idx = phys_world_create_body(&sw->physics);
    if (body_idx == UINT32_MAX || body_idx >= DEMO_MAX_BODIES) {
        return;
    }

    uint32_t rval = xorshift32(&sw->rng_state);

    /* Random angle for horizontal placement. */
    float angle = ((float)(rval & 0xFFFF) / 65535.0f) * 2.0f * PI_F;

    /* Distance: 50-100m from origin. */
    uint32_t rval2 = xorshift32(&sw->rng_state);
    float dist = 50.0f + ((float)(rval2 % 51u));

    /* Height: 30-50m. */
    uint32_t rval3 = xorshift32(&sw->rng_state);
    float height = 30.0f + ((float)(rval3 % 21u));

    phys_body_t *b = phys_world_get_body(&sw->physics, body_idx);
    if (!b) {
        return;
    }

    b->position.x = cosf(angle) * dist;
    b->position.y = height;
    b->position.z = sinf(angle) * dist;

    /* Decide box vs sphere. */
    uint32_t shape_rval = xorshift32(&sw->rng_state);
    int is_sphere = (int)(shape_rval & 1u);

    /* Random size: 1-3m. */
    uint32_t size_rval = xorshift32(&sw->rng_state);
    float size = 1.0f + ((float)(size_rval % 21u)) * 0.1f;

    float mass;
    if (is_sphere) {
        /* Sphere volume = 4/3 * pi * r³ */
        float radius = size * 0.5f;
        mass = BOX_DENSITY * (4.0f / 3.0f) * PI_F * radius * radius * radius;
        phys_body_set_mass(b, mass);
        phys_body_set_sphere_inertia(b, mass, radius);
        phys_world_set_sphere_collider(&sw->physics, body_idx, radius,
                                       (phys_vec3_t){0.0f, 0.0f, 0.0f});
        sw->body_shape_type[body_idx] = 1;
    } else {
        /* Box volume = (2*half)³ = size³ for a cube. */
        float half = size * 0.5f;
        phys_vec3_t half_ext = {half, half, half};
        mass = BOX_DENSITY * size * size * size;
        phys_body_set_mass(b, mass);
        phys_body_set_box_inertia(b, mass, half_ext);
        phys_world_set_box_collider(&sw->physics, body_idx, half_ext,
                                    (phys_vec3_t){0.0f, 0.0f, 0.0f},
                                    (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f});
        sw->body_shape_type[body_idx] = 0;
    }

    /* Copy to next buffer. */
    phys_body_t *b_next = phys_body_pool_get_next(&sw->physics.body_pool,
                                                   body_idx);
    if (b_next) {
        *b_next = *b;
    }

    sw->body_color_seed[body_idx] = xorshift32(&sw->rng_state);
    sw->dynamic_body_count++;
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
    float mass   = volume * BOX_DENSITY;
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

void demo_server_world_tick(demo_server_world_t *sw) {
    if (!sw) {
        return;
    }

    phys_world_tick(&sw->physics, NULL);

    sw->ticks_since_spawn++;

    /* Compute the spawn threshold for this interval (30-60 ticks). */
    uint32_t threshold = DISTANT_SPAWN_MIN +
                         (xorshift32(&sw->rng_state) % DISTANT_SPAWN_RANGE);

    if (sw->ticks_since_spawn >= threshold) {
        spawn_distant_object(sw);
        sw->ticks_since_spawn = 0;
    }
}
