/**
 * @file world_impact.c
 * @brief Impact event retrieval and filtering API.
 *
 * Provides functions to query the per-frame impact event buffer
 * stored in phys_world_t.  Events are populated by the cache_commit
 * stage and consumed by gameplay systems (sound, damage, particles).
 */

#include "ferrum/physics/world.h"
#include "ferrum/physics/cache_commit.h"

#include <stddef.h>

const phys_impact_event_t *phys_world_get_impact_events(
    const phys_world_t *world, uint32_t *out_count)
{
    if (!world) {
        if (out_count) { *out_count = 0; }
        return NULL;
    }
    if (out_count) {
        *out_count = world->impact_event_count;
    }
    return world->impact_events;
}

void phys_world_clear_impact_events(phys_world_t *world)
{
    if (!world) { return; }
    world->impact_event_count = 0;
}

uint32_t phys_world_get_impact_events_for_body(
    const phys_world_t *world, uint32_t body_idx,
    phys_impact_event_t *out_events, uint32_t max_events)
{
    if (!world || !out_events || max_events == 0) {
        return 0;
    }

    uint32_t found = 0;
    for (uint32_t i = 0; i < world->impact_event_count && found < max_events; i++) {
        const phys_impact_event_t *ev = &world->impact_events[i];
        if (ev->body_a == body_idx || ev->body_b == body_idx) {
            out_events[found++] = *ev;
        }
    }
    return found;
}

bool phys_world_get_strongest_impact(
    const phys_world_t *world, uint32_t body_idx,
    phys_impact_event_t *out_event)
{
    if (!world || !out_event) {
        return false;
    }

    bool found = false;
    float max_impulse = -1.0f;

    for (uint32_t i = 0; i < world->impact_event_count; i++) {
        const phys_impact_event_t *ev = &world->impact_events[i];
        if ((ev->body_a == body_idx || ev->body_b == body_idx) &&
            ev->impulse_magnitude > max_impulse) {
            max_impulse = ev->impulse_magnitude;
            *out_event = *ev;
            found = true;
        }
    }
    return found;
}
