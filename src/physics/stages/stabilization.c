/**
 * @file stabilization.c
 * @brief Stage 8: Stabilization Hints.
 *
 * Classifies each manifold's contact as resting or active based on
 * relative velocity at the first contact point, and writes per-manifold
 * friction/restitution scale hints.  Per-tier scaling is applied using
 * the higher tier (lower fidelity) body in each pair.
 *
 * Box contacts on edges or corners are never classified as resting
 * because those configurations are inherently unstable — gravity will
 * always topple the box eventually.
 */

#include "ferrum/physics/stabilization.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"

/**
 * @brief Tolerance factor for face-contact classification.
 *
 * A local-space coordinate is "at the boundary" of a box half-extent
 * when  |coord| >= half_extent * (1 - FACE_TOL).
 * With 0.05 this means within 5% of the edge.
 */
#define FACE_TOL 0.05f

/**
 * @brief Check if a box contact is geometrically unstable (edge/corner).
 *
 * Returns true if the contact point lies on an edge or corner of the
 * box — i.e. two or more of its local-space coordinates are near the
 * half-extent boundary.  Such contacts cannot provide stable support
 * and should not receive resting stabilization hints.
 *
 * @param local       Contact point in body-local space.
 * @param half_ext    Box half-extents.
 * @return true if contact is on an edge or corner (unstable).
 */
static int box_contact_unstable_(phys_vec3_t local, phys_vec3_t half_ext)
{
    /* Count how many axes have the contact point near the boundary. */
    int boundary_count = 0;
    const float coords[3] = { local.x, local.y, local.z };
    const float extents[3] = { half_ext.x, half_ext.y, half_ext.z };

    for (int a = 0; a < 3; ++a) {
        if (extents[a] <= 0.0f) {
            continue;
        }
        float limit = extents[a] * (1.0f - FACE_TOL);
        if (fabsf(coords[a]) >= limit) {
            boundary_count++;
        }
    }

    /* Face contact: exactly 1 axis at the boundary.
     * Edge contact: 2 axes at the boundary.
     * Corner contact: 3 axes at the boundary.
     * 0 axes: interior point (treat as stable). */
    return boundary_count >= 2;
}

/**
 * @brief Check if a manifold contact is on an unstable box support.
 *
 * If collider/box data is available and either body is a box whose
 * contact point sits on an edge or corner, this returns true.
 *
 * @param args  Stage arguments (colliders/boxes may be NULL).
 * @param m     The manifold to check.
 * @param cp    The first contact point.
 * @return true if contact is geometrically unstable for a box body.
 */
static int contact_on_unstable_box_(const phys_stabilization_args_t *args,
                                    const phys_manifold_t *m,
                                    const phys_contact_point_t *cp)
{
    if (!args->colliders || !args->boxes) {
        return 0;
    }

    /* Check body A. */
    const phys_collider_t *ca = &args->colliders[m->body_a];
    if (ca->type == PHYS_SHAPE_BOX) {
        phys_vec3_t he = args->boxes[ca->shape_index].half_extents;
        if (box_contact_unstable_(cp->local_a, he)) {
            return 1;
        }
    }

    /* Check body B. */
    const phys_collider_t *cb = &args->colliders[m->body_b];
    if (cb->type == PHYS_SHAPE_BOX) {
        phys_vec3_t he = args->boxes[cb->shape_index].half_extents;
        if (box_contact_unstable_(cp->local_b, he)) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Check if a manifold's contact points form stable support.
 *
 * Stable support requires at least 3 noncollinear contact points whose
 * support plane is roughly horizontal (normal aligned with gravity axis).
 * A vertical support plane (e.g., side-to-side box contact) cannot resist
 * gravity-induced toppling and must not receive resting stabilization.
 *
 * @param m   The manifold to check.
 * @return true if the contact provides stable (horizontal) support.
 */
static int manifold_has_stable_support_(const phys_manifold_t *m)
{
    if (m->point_count < 3) {
        return 0;
    }

    /* Compute the support plane normal from the first 3 contact points. */
    vec3_t p0 = m->points[0].point_world;
    vec3_t p1 = m->points[1].point_world;
    vec3_t p2 = m->points[2].point_world;

    vec3_t e1 = vec3_sub(p1, p0);
    vec3_t e2 = vec3_sub(p2, p0);
    vec3_t cross = vec3_cross(e1, e2);
    float area_sq = vec3_dot(cross, cross);

    /* Collinearity check: triangle with sides ~1mm has area ~0.5e-6. */
    if (area_sq < 1e-8f) {
        return 0;
    }

    /* The support plane must be roughly horizontal for gravitational
     * stability.  Check that the plane normal has a strong vertical
     * component (|normal.y| / |normal| > cos(45°) ≈ 0.707).
     * Squaring both sides avoids the sqrt: y² > 0.5 * |n|². */
    float ny_sq = cross.y * cross.y;
    return ny_sq > 0.5f * area_sq;
}

void phys_stage_stabilization(const phys_stabilization_args_t *args)
{
    if (!args || !args->manifolds || !args->hints_out || !args->bodies) {
        return;
    }

    const float threshold = args->resting_velocity_threshold;
    const float threshold_sq = threshold * threshold;

    for (uint32_t i = 0; i < args->manifold_count; ++i) {
        const phys_manifold_t *m = &args->manifolds[i];
        phys_stab_hint_t *hint = &args->hints_out[i];

        const phys_body_t *body_a = &args->bodies[m->body_a];
        const phys_body_t *body_b = &args->bodies[m->body_b];

        /* Determine tier: use the higher tier (lower fidelity) body. */
        uint8_t effective_tier = body_a->tier > body_b->tier
                                     ? body_a->tier
                                     : body_b->tier;

        /* Look up per-tier stabilization factors. */
        float tier_friction_boost;
        float tier_velocity_damping;
        phys_tier_stabilization_params((phys_tier_t)effective_tier,
                                       &tier_friction_boost,
                                       &tier_velocity_damping);

        hint->friction_boost   = tier_friction_boost;
        hint->velocity_damping = tier_velocity_damping;

        /* Default to active (no resting boost). */
        hint->friction_scale    = 1.0f;
        hint->restitution_scale = 1.0f;

        if (m->point_count == 0) {
            continue;
        }

        const phys_contact_point_t *cp = &m->points[0];

        /* Lever arms from body centers to contact point. */
        vec3_t r_a = vec3_sub(cp->point_world, body_a->position);
        vec3_t r_b = vec3_sub(cp->point_world, body_b->position);

        /* Velocity at contact point for each body:
         * v = linear_vel + cross(angular_vel, r) */
        vec3_t v_a = vec3_add(body_a->linear_vel,
                              vec3_cross(body_a->angular_vel, r_a));
        vec3_t v_b = vec3_add(body_b->linear_vel,
                              vec3_cross(body_b->angular_vel, r_b));

        /* Relative velocity (A relative to B). */
        vec3_t v_rel = vec3_sub(v_a, v_b);

        /* Normal component of relative velocity. */
        float v_n = vec3_dot(v_rel, cp->normal);

        /* Tangential component squared:
         * |v_t|^2 = |v_rel|^2 - v_n^2 */
        float v_rel_sq = vec3_dot(v_rel, v_rel);
        float v_t_sq = v_rel_sq - v_n * v_n;

        /* Guard against floating-point noise producing negative values. */
        if (v_t_sq < 0.0f) {
            v_t_sq = 0.0f;
        }

        /* Classify as resting if both normal and tangential speeds
         * are below the threshold.  Apply tier friction boost.
         *
         * Exceptions — do NOT apply resting stabilization when:
         * 1. A box body is supported on an edge or corner (unstable).
         * 2. The manifold has fewer than 3 noncollinear contact points,
         *    meaning the support polygon cannot resist torque. */
        if (fabsf(v_n) < threshold && v_t_sq < threshold_sq) {
            if (!contact_on_unstable_box_(args, m, cp) &&
                manifold_has_stable_support_(m)) {
                hint->friction_scale    = 3.0f * tier_friction_boost;
                hint->restitution_scale = 0.0f;
            }
        }
    }
}
