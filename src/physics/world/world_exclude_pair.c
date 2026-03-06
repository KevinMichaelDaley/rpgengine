/**
 * @file world_exclude_pair.c
 * @brief Collision exclusion for body pairs.
 *
 * Wraps the phys_pair_set_t on phys_world_t to provide a clean API
 * for excluding body pairs from narrowphase collision detection.
 *
 * Non-static functions: 2 (phys_world_exclude_pair, phys_world_is_pair_excluded)
 */

#include "ferrum/physics/world.h"

/**
 * @brief Build a canonical pair key: (min(a,b) << 32) | max(a,b).
 */
static uint64_t make_exclude_key_(uint32_t a, uint32_t b) {
    uint32_t lo = a < b ? a : b;
    uint32_t hi = a < b ? b : a;
    return ((uint64_t)lo << 32) | (uint64_t)hi;
}

void phys_world_exclude_pair(phys_world_t *world,
                             uint32_t body_a, uint32_t body_b) {
    if (!world || body_a == body_b) return;
    uint64_t key = make_exclude_key_(body_a, body_b);
    phys_pair_set_upsert(&world->collision_exclude, key, 0);
}

bool phys_world_is_pair_excluded(const phys_world_t *world,
                                 uint32_t body_a, uint32_t body_b) {
    if (!world || body_a == body_b) return false;
    uint64_t key = make_exclude_key_(body_a, body_b);
    return phys_pair_set_contains(&world->collision_exclude, key);
}
