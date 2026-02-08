/**
 * @file manifold_build.c
 * @brief Stage 7: Manifold Build + Cache Merge.
 *
 * Merges narrowphase contact candidates with the persistent manifold
 * cache so that accumulated impulses survive across frames (warmstarting).
 */

#include "ferrum/physics/manifold_build.h"

#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/narrowphase.h"

#include <string.h>

/* ── Public API (1 non-static function) ─────────────────────────── */

void phys_stage_manifold_build(const phys_manifold_build_args_t *args)
{
    if (!args || !args->candidates || !args->cache
        || !args->manifolds_out || !args->manifold_count_out) {
        if (args && args->manifold_count_out) {
            *args->manifold_count_out = 0;
        }
        return;
    }

    uint32_t manifold_count = 0;

    for (uint32_t i = 0; i < args->candidate_count; ++i) {
        const phys_contact_candidate_t *cand = &args->candidates[i];

        /* 1. Get or create cached manifold for this body pair. */
        phys_manifold_t *cached = phys_manifold_cache_get_or_create(
            args->cache, cand->body_a, cand->body_b, (uint32_t)args->tick);
        if (!cached) {
            continue; /* cache full */
        }

        /* 2. Save old impulses and feature IDs for warmstart matching. */
        float old_normal[PHYS_MAX_MANIFOLD_POINTS];
        float old_tangent[PHYS_MAX_MANIFOLD_POINTS][2];
        uint32_t old_features[PHYS_MAX_MANIFOLD_POINTS];
        uint8_t old_count = cached->point_count;

        for (uint8_t j = 0; j < old_count; ++j) {
            old_normal[j]     = cached->normal_impulse[j];
            old_tangent[j][0] = cached->tangent_impulse[j][0];
            old_tangent[j][1] = cached->tangent_impulse[j][1];
            old_features[j]   = cached->points[j].feature_id;
        }

        /* 3. Clear manifold, zero impulses, and set body IDs. */
        phys_manifold_clear(cached);
        for (uint8_t j = 0; j < PHYS_MAX_MANIFOLD_POINTS; ++j) {
            cached->normal_impulse[j]     = 0.0f;
            cached->tangent_impulse[j][0] = 0.0f;
            cached->tangent_impulse[j][1] = 0.0f;
        }
        cached->body_a = cand->body_a;
        cached->body_b = cand->body_b;

        /* Combine surface material from both bodies. */
        if (args->bodies) {
            cached->friction = phys_combine_friction(
                args->bodies[cand->body_a].friction,
                args->bodies[cand->body_b].friction);
            cached->restitution = phys_combine_restitution(
                args->bodies[cand->body_a].restitution,
                args->bodies[cand->body_b].restitution);
        }

        /* 4. Add new contact points from the candidate. */
        for (uint8_t j = 0; j < cand->contact_count; ++j) {
            phys_manifold_add_point(cached, &cand->contacts[j]);
        }

        /* 5. Match new contacts to old by feature_id → restore impulses. */
        for (uint8_t j = 0; j < cached->point_count; ++j) {
            uint32_t feat = cached->points[j].feature_id;
            for (uint8_t k = 0; k < old_count; ++k) {
                if (old_features[k] == feat) {
                    cached->normal_impulse[j]     = old_normal[k];
                    cached->tangent_impulse[j][0]  = old_tangent[k][0];
                    cached->tangent_impulse[j][1]  = old_tangent[k][1];
                    break;
                }
            }
        }

        /* 6. Copy to output buffer. */
        if (manifold_count < args->max_manifolds) {
            args->manifolds_out[manifold_count++] = *cached;
        }
    }

    *args->manifold_count_out = manifold_count;
}
