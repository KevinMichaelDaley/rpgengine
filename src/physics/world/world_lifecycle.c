#include "ferrum/physics/world.h"

#include <stdlib.h>
#include <string.h>

int phys_world_init(phys_world_t *world, const phys_world_config_t *config) {
    if (!world || !config) {
        return -1;
    }

    memset(world, 0, sizeof(*world));
    world->config = *config;

    /* Initialize body pool. */
    if (phys_body_pool_init(&world->body_pool, config->max_bodies) != 0) {
        return -1;
    }

    /* Allocate per-body collider and AABB arrays. */
    world->colliders = calloc(config->max_bodies, sizeof(phys_collider_t));
    world->aabbs     = calloc(config->max_bodies, sizeof(phys_aabb_t));
    if (!world->colliders || !world->aabbs) {
        phys_world_destroy(world);
        return -1;
    }

    /* Allocate shape arrays (max_bodies is an upper bound). */
    world->spheres  = calloc(config->max_bodies, sizeof(phys_sphere_t));
    world->boxes    = calloc(config->max_bodies, sizeof(phys_box_t));
    world->capsules = calloc(config->max_bodies, sizeof(phys_capsule_t));
    if (!world->spheres || !world->boxes || !world->capsules) {
        phys_world_destroy(world);
        return -1;
    }

    /* Initialize manifold cache. */
    if (phys_manifold_cache_init(&world->manifold_cache,
                                 config->manifold_cache_size) != 0) {
        phys_world_destroy(world);
        return -1;
    }

    /* Initialize frame arena. */
    if (phys_frame_arena_init(&world->frame_arena,
                              config->frame_arena_size) != 0) {
        phys_world_destroy(world);
        return -1;
    }

    world->tick_count = 0;
    return 0;
}

void phys_world_destroy(phys_world_t *world) {
    if (!world) {
        return;
    }

    phys_body_pool_destroy(&world->body_pool);

    free(world->colliders);
    free(world->aabbs);
    free(world->spheres);
    free(world->boxes);
    free(world->capsules);

    phys_manifold_cache_destroy(&world->manifold_cache);
    phys_frame_arena_destroy(&world->frame_arena);

    memset(world, 0, sizeof(*world));
}
