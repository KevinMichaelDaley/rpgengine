/**
 * @file narrowphase.c
 * @brief Narrowphase contact generation stage.
 *
 * Iterates broadphase pairs, dispatches to shape-specific tests,
 * and outputs contact candidates.  Handles all 6 primitive
 * collider pairs: sphere-sphere, sphere-box, sphere-capsule,
 * box-box, box-capsule, capsule-capsule.
 */

#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/narrowphase.h"
#include "ferrum/physics/tier_list.h"
#include "ferrum/physics/collision/box_box.h"
#include "ferrum/physics/collision/box_capsule.h"
#include "ferrum/physics/collision/capsule_capsule.h"
#include "ferrum/physics/collision/sphere_simplify.h"

/**
 * @brief Emit a single-contact candidate into the output buffer.
 *
 * Helper to avoid repeating the write-out pattern for every
 * shape pair that produces exactly one contact point.
 */
static void emit_single(phys_contact_candidate_t *cand,
                         uint32_t body_a, uint32_t body_b,
                         const phys_contact_point_t *c)
{
    cand->body_a = body_a;
    cand->body_b = body_b;
    cand->contacts[0] = *c;
    cand->contact_count = 1;
}

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

        /* Compute world-space transforms for both bodies. */
        phys_vec3_t wa = phys_collider_world_center(
            ca, args->bodies[a].position, args->bodies[a].orientation);
        phys_quat_t qa = phys_collider_world_rotation(
            ca, args->bodies[a].orientation);

        phys_vec3_t wb = phys_collider_world_center(
            cb, args->bodies[b].position, args->bodies[b].orientation);
        phys_quat_t qb = phys_collider_world_rotation(
            cb, args->bodies[b].orientation);

        /* Normalize the pair so that type_a <= type_b to reduce cases. */
        uint32_t ba = a, bb = b;
        const phys_collider_t *c0 = ca, *c1 = cb;
        phys_vec3_t w0 = wa, w1 = wb;
        phys_quat_t q0 = qa, q1 = qb;
        if (c0->type > c1->type) {
            /* Swap so c0 is the "lower" type. */
            ba = b; bb = a;
            c0 = cb; c1 = ca;
            w0 = wb; w1 = wa;
            q0 = qb; q1 = qa;
        }

        phys_contact_point_t contact;
        memset(&contact, 0, sizeof(contact));
        bool hit = false;

        /* Sphere simplification at distance (T2+): if both colliders are
         * flagged, run a cheap sphere-sphere test using their bounding
         * spheres instead of the full shape test. */
        uint8_t tier_a = args->bodies[ba].tier;
        uint8_t tier_b = args->bodies[bb].tier;
        if (tier_a >= PHYS_TIER_2_VISIBLE && tier_b >= PHYS_TIER_2_VISIBLE
            && c0->sphere_simplify && c1->sphere_simplify) {
            float ra = phys_sphere_simplify_radius(
                c0, args->spheres, args->boxes, args->capsules);
            float rb = phys_sphere_simplify_radius(
                c1, args->spheres, args->boxes, args->capsules);
            if (ra > 0.0f && rb > 0.0f) {
                hit = phys_sphere_vs_sphere(w0, ra, w1, rb, &contact);
                if (hit) {
                    emit_single(&args->candidates_out[count], ba, bb, &contact);
                    count++;
                }
                continue;
            }
        }

        /* Dispatch on normalized (type_lo, type_hi) pair. */
        if (c0->type == PHYS_SHAPE_SPHERE && c1->type == PHYS_SHAPE_SPHERE) {
            float r0 = args->spheres[c0->shape_index].radius;
            float r1 = args->spheres[c1->shape_index].radius;
            hit = phys_sphere_vs_sphere(w0, r0, w1, r1, &contact);
        }
        else if (c0->type == PHYS_SHAPE_SPHERE && c1->type == PHYS_SHAPE_BOX) {
            float rs = args->spheres[c0->shape_index].radius;
            phys_vec3_t he = args->boxes[c1->shape_index].half_extents;
            hit = phys_sphere_vs_box(w0, rs, w1, q1, he, &contact);
        }
        else if (c0->type == PHYS_SHAPE_SPHERE && c1->type == PHYS_SHAPE_CAPSULE) {
            float rs = args->spheres[c0->shape_index].radius;
            float rc = args->capsules[c1->shape_index].radius;
            float hh = args->capsules[c1->shape_index].half_height;
            hit = phys_sphere_vs_capsule(w0, rs, w1, q1, rc, hh, &contact);
        }
        else if (c0->type == PHYS_SHAPE_BOX && c1->type == PHYS_SHAPE_BOX) {
            phys_vec3_t he0 = args->boxes[c0->shape_index].half_extents;
            phys_vec3_t he1 = args->boxes[c1->shape_index].half_extents;
            phys_contact_point_t contacts_buf[4];
            int nc = phys_box_vs_box(w0, q0, he0, w1, q1, he1,
                                     contacts_buf, 4);
            if (nc > 0) {
                phys_contact_candidate_t *cand = &args->candidates_out[count];
                cand->body_a = ba;
                cand->body_b = bb;
                cand->contact_count = (uint8_t)(nc > 4 ? 4 : nc);
                for (int j = 0; j < cand->contact_count; j++) {
                    cand->contacts[j] = contacts_buf[j];
                }
                count++;
                continue;  /* Already emitted. */
            }
        }
        else if (c0->type == PHYS_SHAPE_BOX && c1->type == PHYS_SHAPE_CAPSULE) {
            phys_vec3_t he = args->boxes[c0->shape_index].half_extents;
            float rc = args->capsules[c1->shape_index].radius;
            float hh = args->capsules[c1->shape_index].half_height;
            hit = phys_box_vs_capsule(w0, q0, he, w1, q1, rc, hh, &contact);
        }
        else if (c0->type == PHYS_SHAPE_CAPSULE && c1->type == PHYS_SHAPE_CAPSULE) {
            float r0c = args->capsules[c0->shape_index].radius;
            float h0  = args->capsules[c0->shape_index].half_height;
            float r1c = args->capsules[c1->shape_index].radius;
            float h1  = args->capsules[c1->shape_index].half_height;
            hit = phys_capsule_vs_capsule(w0, q0, r0c, h0,
                                          w1, q1, r1c, h1, &contact);
        }

        if (hit) {
            emit_single(&args->candidates_out[count], ba, bb, &contact);
            count++;
        }
    }

    *args->candidate_count_out = count;
}
