/**
 * @file world_joint.c
 * @brief World joint management: add, remove, get, count.
 */

#include "ferrum/physics/world.h"

#include <string.h>

uint32_t phys_world_add_joint(phys_world_t *world, const phys_joint_t *joint) {
    if (!world || !joint) { return UINT32_MAX; }
    if (world->joint_count >= world->joint_capacity) { return UINT32_MAX; }

    uint32_t idx = world->joint_count;
    world->joints[idx] = *joint;
    world->joint_count++;
    return idx;
}

void phys_world_remove_joint(phys_world_t *world, uint32_t index) {
    if (!world || index >= world->joint_count) { return; }

    /* Swap-remove: move last joint into the removed slot. */
    uint32_t last = world->joint_count - 1;
    if (index != last) {
        world->joints[index] = world->joints[last];
    }
    memset(&world->joints[last], 0, sizeof(phys_joint_t));
    world->joint_count--;
}

phys_joint_t *phys_world_get_joint(phys_world_t *world, uint32_t index) {
    if (!world || index >= world->joint_count) { return NULL; }
    return &world->joints[index];
}

uint32_t phys_world_joint_count(const phys_world_t *world) {
    if (!world) { return 0; }
    return world->joint_count;
}
