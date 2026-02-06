/**
 * @file narrowphase.c
 * @brief Narrowphase contact generation stage.
 *
 * Iterates broadphase pairs, dispatches to shape-specific tests,
 * and outputs contact candidates.  Phase 1: sphere-sphere only.
 */

#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/narrowphase.h"

void phys_stage_narrowphase(const phys_narrowphase_args_t *args)
{
    if (!args || !args->bodies || !args->colliders || !args->pairs
        || !args->candidates_out || !args->candidate_count_out) {
        return;
    }

    uint32_t count = 0;

    for (uint32_t i = 0; i < args->pair_count && count < args->max_candidates; i++) {
        uint32_t a = args->pairs[i].body_a;
        uint32_t b = args->pairs[i].body_b;

        const phys_collider_t *ca = &args->colliders[a];
        const phys_collider_t *cb = &args->colliders[b];

        /* Phase 1: sphere-sphere only. */
        if (ca->type == PHYS_SHAPE_SPHERE && cb->type == PHYS_SHAPE_SPHERE) {
            /* Compute world-space centers. */
            phys_vec3_t wa = phys_collider_world_center(
                ca, args->bodies[a].position, args->bodies[a].orientation);
            phys_vec3_t wb = phys_collider_world_center(
                cb, args->bodies[b].position, args->bodies[b].orientation);

            float ra = args->spheres[ca->shape_index].radius;
            float rb = args->spheres[cb->shape_index].radius;

            phys_contact_point_t contact;
            memset(&contact, 0, sizeof(contact));

            if (phys_sphere_vs_sphere(wa, ra, wb, rb, &contact)) {
                phys_contact_candidate_t *cand = &args->candidates_out[count];
                cand->body_a = a;
                cand->body_b = b;
                cand->contacts[0] = contact;
                cand->contact_count = 1;
                count++;
            }
        }
        /* Other shape combos: skip in Phase 1. */
    }

    *args->candidate_count_out = count;
}
