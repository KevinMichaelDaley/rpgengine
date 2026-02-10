#include "ferrum/server/physics/sync/pre_physics_sync.h"

#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"

int phys_pre_physics_sync(const phys_pre_physics_sync_args_t *args) {
    if (!args || !args->body_pool) {
        return -1;
    }
    if (args->record_count == 0) {
        return 0;
    }
    if (!args->records) {
        return -1;
    }

    phys_body_pool_t *pool = args->body_pool;

    for (uint32_t i = 0; i < args->record_count; ++i) {
        const phys_sync_record_t *r = &args->records[i];
        if (!r->dirty) {
            continue;
        }

        phys_body_t *body = phys_body_pool_get_next(pool, r->body_index);
        if (!body) {
            continue;
        }

        body->linear_vel = r->linear_vel;
        body->position = r->position;
        body->entity_index = r->entity_index;
    }

    return 0;
}
