#include "ferrum/physics/world.h"
#include "ferrum/physics/cache_commit.h"

#include <stdlib.h>
#include <string.h>

/** Default capacity for the impact event buffer. */
#define IMPACT_EVENT_DEFAULT_CAPACITY 256

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
    world->meshes   = calloc(config->max_bodies, sizeof(phys_mesh_shape_t));
    world->halfspaces = calloc(config->max_bodies, sizeof(phys_halfspace_t));
    if (!world->spheres || !world->boxes || !world->capsules || !world->meshes
        || !world->halfspaces) {
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

    /* Allocate impact event buffer. */
    world->impact_events = calloc(IMPACT_EVENT_DEFAULT_CAPACITY,
                                  sizeof(phys_impact_event_t));
    if (!world->impact_events) {
        phys_world_destroy(world);
        return -1;
    }
    world->impact_event_count    = 0;
    world->impact_event_capacity = IMPACT_EVENT_DEFAULT_CAPACITY;
    world->impact_threshold      = 1.0f;

    /* Allocate joint array. */
    uint32_t max_joints = config->max_joints;
    if (max_joints > 0) {
        world->joints = calloc(max_joints, sizeof(phys_joint_t));
        if (!world->joints) {
            phys_world_destroy(world);
            return -1;
        }
    }
    world->joint_count    = 0;
    world->joint_capacity = max_joints;

    /* Allocate CCD previous-frame body buffer (triple-buffer). */
    world->bodies_ccd_prev = calloc(config->max_bodies, sizeof(phys_body_t));
    if (!world->bodies_ccd_prev) {
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
    free(world->meshes);
    free(world->halfspaces);
    free(world->impact_events);
    free(world->joints);
    free(world->bodies_ccd_prev);

    free(world->static_bucket_flags);

    phys_manifold_cache_destroy(&world->manifold_cache);
    phys_frame_arena_destroy(&world->static_bvh_arena);
    phys_frame_arena_destroy(&world->frame_arena);

    memset(world, 0, sizeof(*world));
}
