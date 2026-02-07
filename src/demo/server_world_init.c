/**
 * @file server_world_init.c
 * @brief Demo server world initialization and destruction.
 *
 * Creates the physics world with appropriate config and sets up
 * the static ground plane body.
 */

#include "ferrum/demo/server_world.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/phys_pool.h"

#include <string.h>

/* ── Public API (2 non-static functions) ───────────────────────── */

int demo_server_world_init(demo_server_world_t *sw, uint32_t rng_seed) {
    if (!sw) {
        return -1;
    }

    memset(sw, 0, sizeof(*sw));
    sw->rng_state = rng_seed ? rng_seed : 12345u;

    /* Configure the physics world for the demo scene. */
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies    = DEMO_MAX_BODIES;
    cfg.max_colliders = DEMO_MAX_BODIES;
    cfg.gravity       = (phys_vec3_t){0.0f, -9.81f, 0.0f};
    cfg.fixed_dt      = 1.0f / 60.0f;

    if (phys_world_init(&sw->physics, &cfg) != 0) {
        return -1;
    }

    /* Create the static ground plane: box at y = -0.5 with half-extents (200, 0.5, 200).
     * Must be large enough to catch all distant spawns (up to ~100m from origin). */
    uint32_t ground = phys_world_create_body(&sw->physics);
    if (ground == UINT32_MAX) {
        phys_world_destroy(&sw->physics);
        return -1;
    }

    phys_body_t *gb = phys_world_get_body(&sw->physics, ground);
    gb->position = (phys_vec3_t){0.0f, -0.5f, 0.0f};
    gb->flags    = PHYS_BODY_FLAG_STATIC;
    gb->inv_mass = 0.0f;

    /* Copy to next buffer so tick reads consistent state. */
    phys_body_t *gb_next = phys_body_pool_get_next(&sw->physics.body_pool, ground);
    *gb_next = *gb;

    phys_world_set_box_collider(&sw->physics, ground,
                                (phys_vec3_t){200.0f, 0.5f, 200.0f},
                                (phys_vec3_t){0.0f, 0.0f, 0.0f},
                                (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f});

    /* Mark all player slots as empty. */
    for (int i = 0; i < DEMO_MAX_CLIENTS; i++) {
        sw->player_body[i] = UINT32_MAX;
    }

    return 0;
}

void demo_server_world_destroy(demo_server_world_t *sw) {
    if (!sw) {
        return;
    }
    phys_world_destroy(&sw->physics);
}
