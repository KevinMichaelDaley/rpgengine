/**
 * @file cache_commit.c
 * @brief Stage 13: Cache Commit + Events.
 *
 * Writes solved impulses back into the manifold cache for next-frame
 * warmstarting and emits impact events for gameplay systems.
 */

#include "ferrum/physics/cache_commit.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/physics/constraint.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/manifold_cache.h"

void phys_stage_cache_commit(const phys_cache_commit_args_t *args)
{
    if (!args) {
        return;
    }

    uint32_t event_count = 0;

    for (uint32_t i = 0; i < args->constraint_count; ++i) {
        const phys_constraint_t *c = &args->constraints[i];

        /* Look up the cached manifold for this body pair. */
        phys_manifold_t *cached = phys_manifold_cache_find(
            args->cache, c->body_a, c->body_b);

        /* Write back solved impulses for warmstarting. */
        if (cached && c->point_idx < cached->point_count) {
            cached->normal_impulse[c->point_idx]      = c->rows[0].lambda;
            cached->tangent_impulse[c->point_idx][0]   = c->rows[1].lambda;
            cached->tangent_impulse[c->point_idx][1]   = c->rows[2].lambda;
        }

        /* Emit impact event if normal impulse exceeds threshold. */
        float impulse = fabsf(c->rows[0].lambda);
        if (impulse > args->impact_threshold &&
            event_count < args->max_events &&
            args->events_out) {
            const phys_manifold_t *m = &args->manifolds[c->manifold_idx];

            args->events_out[event_count] = (phys_impact_event_t){
                .body_a             = c->body_a,
                .body_b             = c->body_b,
                .point              = m->points[c->point_idx].point_world,
                .normal             = m->points[c->point_idx].normal,
                .impulse_magnitude  = impulse,
            };
            event_count++;
        }
    }

    if (args->event_count_out) {
        *args->event_count_out = event_count;
    }
}
