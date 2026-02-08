/**
 * @file constraint_build.c
 * @brief Build a contact constraint (1 normal + 2 friction rows).
 */

#include "ferrum/physics/constraint.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/math/vec3.h"

#include <float.h>
#include <string.h>

/* Large clamp value used instead of FLT_MAX for numerical safety. */
#define CONSTRAINT_LAMBDA_BIG 1e10f

/**
 * @brief Build a single Jacobian row for a direction (normal or tangent).
 *
 * @param row   Output row.
 * @param rA    Lever arm from body A center to contact point.
 * @param rB    Lever arm from body B center to contact point.
 * @param dir   Constraint direction (normal or tangent).
 */
static void build_row_for_direction(phys_jacobian_row_t *row,
                                    phys_vec3_t rA,
                                    phys_vec3_t rB,
                                    phys_vec3_t dir) {
    row->J_va = vec3_scale(dir, -1.0f);
    row->J_wa = vec3_scale(vec3_cross(rA, dir), -1.0f);
    row->J_vb = dir;
    row->J_wb = vec3_cross(rB, dir);
    row->lambda = 0.0f;
}

/**
 * @brief Compute the relative normal velocity at the contact point.
 */
static float compute_relative_normal_velocity(
    const struct phys_body *body_a,
    const struct phys_body *body_b,
    phys_vec3_t rA,
    phys_vec3_t rB,
    phys_vec3_t normal)
{
    /* v_rel = (vb + wb×rB) - (va + wa×rA) */
    phys_vec3_t va_point = vec3_add(body_a->linear_vel,
                                    vec3_cross(body_a->angular_vel, rA));
    phys_vec3_t vb_point = vec3_add(body_b->linear_vel,
                                    vec3_cross(body_b->angular_vel, rB));
    phys_vec3_t v_rel = vec3_sub(vb_point, va_point);
    return vec3_dot(v_rel, normal);
}

void phys_constraint_build_contact(
    phys_constraint_t *c,
    const struct phys_body *body_a,
    const struct phys_body *body_b,
    const struct phys_contact_point *contact,
    float friction,
    float restitution,
    float dt,
    float baumgarte,
    float slop)
{
    if (!c || !body_a || !body_b || !contact) { return; }

    memset(c, 0, sizeof(*c));
    c->row_count    = 3;
    c->friction     = friction;
    c->penetration  = contact->penetration;

    /* Lever arms from body centers to the contact point. */
    phys_vec3_t rA = vec3_sub(contact->point_world, body_a->position);
    phys_vec3_t rB = vec3_sub(contact->point_world, body_b->position);
    phys_vec3_t normal = contact->normal;

    /* ── Row 0: Normal constraint ─────────────────────────────────── */
    build_row_for_direction(&c->rows[0], rA, rB, normal);
    c->rows[0].lambda_min = 0.0f;
    c->rows[0].lambda_max = CONSTRAINT_LAMBDA_BIG;

    /* Baumgarte bias: -(baumgarte/dt) * max(penetration - slop, 0) */
    float pen_excess = contact->penetration - slop;
    if (pen_excess < 0.0f) { pen_excess = 0.0f; }
    float baumgarte_bias = 0.0f;
    if (dt > 0.0f) {
        baumgarte_bias = (baumgarte / dt) * pen_excess;
    }

    /* Restitution bias: when objects are approaching (vn_rel < 0),
     * add a positive bias proportional to the closing speed to create
     * bounce.  Only apply when closing speed exceeds a small threshold
     * to avoid jitter at rest. */
    float vn_rel = compute_relative_normal_velocity(body_a, body_b,
                                                     rA, rB, normal);
    float restitution_bias = 0.0f;
    if (vn_rel < -0.5f) {
        restitution_bias = -restitution * vn_rel;
    }

    /* Total bias pushes bodies apart. */
    c->rows[0].bias = baumgarte_bias + restitution_bias;

    /* Compute effective mass for normal row. */
    c->rows[0].effective_mass = phys_compute_effective_mass(
        &c->rows[0],
        body_a->inv_mass, &body_a->inv_inertia_diag,
        body_b->inv_mass, &body_b->inv_inertia_diag);

    /* ── Tangent basis ────────────────────────────────────────────── */
    phys_vec3_t tangent1, tangent2;
    phys_compute_tangent_basis(normal, &tangent1, &tangent2);

    /* ── Row 1: Friction tangent 1 ────────────────────────────────── */
    build_row_for_direction(&c->rows[1], rA, rB, tangent1);
    c->rows[1].lambda_min = -CONSTRAINT_LAMBDA_BIG;
    c->rows[1].lambda_max =  CONSTRAINT_LAMBDA_BIG;
    c->rows[1].bias = 0.0f;
    c->rows[1].effective_mass = phys_compute_effective_mass(
        &c->rows[1],
        body_a->inv_mass, &body_a->inv_inertia_diag,
        body_b->inv_mass, &body_b->inv_inertia_diag);

    /* ── Row 2: Friction tangent 2 ────────────────────────────────── */
    build_row_for_direction(&c->rows[2], rA, rB, tangent2);
    c->rows[2].lambda_min = -CONSTRAINT_LAMBDA_BIG;
    c->rows[2].lambda_max =  CONSTRAINT_LAMBDA_BIG;
    c->rows[2].bias = 0.0f;
    c->rows[2].effective_mass = phys_compute_effective_mass(
        &c->rows[2],
        body_a->inv_mass, &body_a->inv_inertia_diag,
        body_b->inv_mass, &body_b->inv_inertia_diag);
}
