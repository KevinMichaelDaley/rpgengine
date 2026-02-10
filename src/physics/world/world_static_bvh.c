#include "ferrum/physics/world.h"

#include <stdlib.h>

void phys_world_static_bvh_invalidate(phys_world_t *world) {
    if (!world) {
        return;
    }

    world->static_bvh_valid = 0;
    world->static_bvh = (phys_static_bvh_t){0};

    free(world->static_bucket_flags);
    world->static_bucket_flags = NULL;
    world->static_bucket_flag_count = 0;

    phys_frame_arena_destroy(&world->static_bvh_arena);
}
