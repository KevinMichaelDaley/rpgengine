/**
 * @file world_impact_config.c
 * @brief Impact event threshold configuration.
 *
 * Allows gameplay code to tune the minimum impulse magnitude
 * required for an impact event to be emitted.
 */

#include "ferrum/physics/world.h"

#include <stddef.h>

void phys_world_set_impact_threshold(phys_world_t *world, float threshold)
{
    if (!world) { return; }
    /* Clamp negative values to zero. */
    world->impact_threshold = (threshold < 0.0f) ? 0.0f : threshold;
}

float phys_world_get_impact_threshold(const phys_world_t *world)
{
    if (!world) { return 0.0f; }
    return world->impact_threshold;
}
