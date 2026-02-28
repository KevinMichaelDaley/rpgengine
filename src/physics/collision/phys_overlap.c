/**
 * @file phys_overlap.c
 * @brief Boolean overlap test between two colliders.
 *
 * Dispatches to the appropriate narrowphase function based on shape types.
 * Non-static functions: 1 (phys_test_overlap).
 */

#include "ferrum/physics/phys_overlap.h"

#include <math.h>

#include "ferrum/physics/narrowphase.h"
#include "ferrum/physics/narrowphase_convex.h"
#include "ferrum/physics/mesh_narrowphase.h"
#include "ferrum/physics/collision/box_box.h"
#include "ferrum/physics/collision/box_capsule.h"
#include "ferrum/physics/collision/capsule_capsule.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/convex_compound.h"

#include <stddef.h>

/* ── Helper: compute world transform for a collider ──────────────── */

static phys_vec3_t world_center_(const phys_collider_t *c,
                                  phys_vec3_t body_pos,
                                  phys_quat_t body_rot) {
    return phys_collider_world_center(c, body_pos, body_rot);
}

static phys_quat_t world_rot_(const phys_collider_t *c,
                               phys_quat_t body_rot) {
    return phys_collider_world_rotation(c, body_rot);
}

/* ── Sphere bounding radius for cheap pre-filter ────────────────── */

static float bounding_radius_(const phys_overlap_ctx_t *ctx,
                               const phys_collider_t *c) {
    switch (c->type) {
    case PHYS_SHAPE_SPHERE:
        return ctx->spheres[c->shape_index].radius;
    case PHYS_SHAPE_BOX: {
        phys_vec3_t h = ctx->boxes[c->shape_index].half_extents;
        /* Bounding sphere radius = length of half-extent diagonal. */
        return sqrtf(h.x * h.x + h.y * h.y + h.z * h.z);
    }
    case PHYS_SHAPE_CAPSULE: {
        const phys_capsule_t *cap = &ctx->capsules[c->shape_index];
        return cap->radius + cap->half_height;
    }
    case PHYS_SHAPE_CONVEX: {
        /* Compute bounding radius from AABB half-diagonal. */
        const phys_convex_hull_t *hull = &ctx->convex_hulls[c->shape_index];
        float hx = (hull->aabb.max.x - hull->aabb.min.x) * 0.5f;
        float hy = (hull->aabb.max.y - hull->aabb.min.y) * 0.5f;
        float hz = (hull->aabb.max.z - hull->aabb.min.z) * 0.5f;
        return sqrtf(hx * hx + hy * hy + hz * hz);
    }
    case PHYS_SHAPE_MESH:
    case PHYS_SHAPE_COMPOUND:
    case PHYS_SHAPE_HALFSPACE:
        return 1000.0f; /* Conservative fallback — always passes pre-filter. */
    default:
        return 0.0f;
    }
}

/* ── Ordered dispatch (ensures type_a <= type_b) ─────────────────── */

static bool overlap_ordered_(const phys_overlap_ctx_t *ctx,
                              const phys_collider_t *ca, phys_vec3_t wa, phys_quat_t ra,
                              const phys_collider_t *cb, phys_vec3_t wb, phys_quat_t rb) {
    struct phys_contact_point cp;

    phys_shape_type_t ta = ca->type;
    phys_shape_type_t tb = cb->type;

    /* Sphere (0) vs ... */
    if (ta == PHYS_SHAPE_SPHERE && tb == PHYS_SHAPE_SPHERE) {
        return phys_sphere_vs_sphere(
            wa, ctx->spheres[ca->shape_index].radius,
            wb, ctx->spheres[cb->shape_index].radius,
            0.0f, &cp);
    }
    if (ta == PHYS_SHAPE_SPHERE && tb == PHYS_SHAPE_BOX) {
        return phys_sphere_vs_box(
            wa, ctx->spheres[ca->shape_index].radius,
            wb, rb, ctx->boxes[cb->shape_index].half_extents,
            0.0f, &cp);
    }
    if (ta == PHYS_SHAPE_SPHERE && tb == PHYS_SHAPE_CAPSULE) {
        const phys_capsule_t *cap = &ctx->capsules[cb->shape_index];
        return phys_sphere_vs_capsule(
            wa, ctx->spheres[ca->shape_index].radius,
            wb, rb, cap->radius, cap->half_height,
            0.0f, &cp);
    }
    if (ta == PHYS_SHAPE_SPHERE && tb == PHYS_SHAPE_CONVEX) {
        return phys_sphere_vs_convex(
            wa, ctx->spheres[ca->shape_index].radius,
            wb, rb, &ctx->convex_hulls[cb->shape_index],
            0.0f, &cp);
    }
    if (ta == PHYS_SHAPE_SPHERE && tb == PHYS_SHAPE_MESH) {
        const phys_mesh_shape_t *m = &ctx->meshes[cb->shape_index];
        return phys_sphere_vs_mesh(
            wa, ctx->spheres[ca->shape_index].radius,
            m->triangles, &m->bvh, 0.0f, m->solid,
            &cp, 1) > 0;
    }

    /* Box (1) vs ... */
    if (ta == PHYS_SHAPE_BOX && tb == PHYS_SHAPE_BOX) {
        return phys_box_vs_box(
            wa, ra, ctx->boxes[ca->shape_index].half_extents,
            wb, rb, ctx->boxes[cb->shape_index].half_extents,
            &cp, 1, 0.0f) > 0;
    }
    if (ta == PHYS_SHAPE_BOX && tb == PHYS_SHAPE_CAPSULE) {
        const phys_capsule_t *cap = &ctx->capsules[cb->shape_index];
        return phys_box_vs_capsule(
            wa, ra, ctx->boxes[ca->shape_index].half_extents,
            wb, rb, cap->radius, cap->half_height,
            0.0f, &cp);
    }
    if (ta == PHYS_SHAPE_BOX && tb == PHYS_SHAPE_CONVEX) {
        return phys_box_vs_convex(
            wa, ra, ctx->boxes[ca->shape_index].half_extents,
            wb, rb, &ctx->convex_hulls[cb->shape_index],
            0.0f, &cp);
    }
    if (ta == PHYS_SHAPE_BOX && tb == PHYS_SHAPE_MESH) {
        const phys_mesh_shape_t *m = &ctx->meshes[cb->shape_index];
        return phys_box_vs_mesh(
            wa, ra, ctx->boxes[ca->shape_index].half_extents,
            m->triangles, &m->bvh, 0.0f,
            &cp, 1) > 0;
    }

    /* Capsule (2) vs ... */
    if (ta == PHYS_SHAPE_CAPSULE && tb == PHYS_SHAPE_CAPSULE) {
        const phys_capsule_t *a = &ctx->capsules[ca->shape_index];
        const phys_capsule_t *b = &ctx->capsules[cb->shape_index];
        return phys_capsule_vs_capsule(
            wa, ra, a->radius, a->half_height,
            wb, rb, b->radius, b->half_height,
            0.0f, &cp);
    }
    if (ta == PHYS_SHAPE_CAPSULE && tb == PHYS_SHAPE_CONVEX) {
        const phys_capsule_t *cap = &ctx->capsules[ca->shape_index];
        return phys_capsule_vs_convex(
            wa, ra, cap->radius, cap->half_height,
            wb, rb, &ctx->convex_hulls[cb->shape_index],
            0.0f, &cp);
    }
    if (ta == PHYS_SHAPE_CAPSULE && tb == PHYS_SHAPE_MESH) {
        const phys_capsule_t *cap = &ctx->capsules[ca->shape_index];
        const phys_mesh_shape_t *m = &ctx->meshes[cb->shape_index];
        return phys_capsule_vs_mesh(
            wa, ra, cap->radius, cap->half_height,
            m->triangles, &m->bvh, 0.0f, m->solid,
            &cp, 1) > 0;
    }

    /* Convex (4) vs Convex (4) */
    if (ta == PHYS_SHAPE_CONVEX && tb == PHYS_SHAPE_CONVEX) {
        return phys_convex_vs_convex(
            wa, ra, &ctx->convex_hulls[ca->shape_index],
            wb, rb, &ctx->convex_hulls[cb->shape_index],
            0.0f, &cp);
    }

    /* Unsupported pair (halfspace×halfspace, mesh×mesh, compound, etc). */
    return false;
}

/* ── Public API ──────────────────────────────────────────────────── */

bool phys_test_overlap(const phys_overlap_ctx_t *ctx,
                       const phys_collider_t *col_a,
                       phys_vec3_t pos_a, phys_quat_t rot_a,
                       const phys_collider_t *col_b,
                       phys_vec3_t pos_b, phys_quat_t rot_b) {
    if (!ctx || !col_a || !col_b) return false;

    /* World-space centers and rotations for each collider. */
    phys_vec3_t wa = world_center_(col_a, pos_a, rot_a);
    phys_quat_t ra = world_rot_(col_a, rot_a);
    phys_vec3_t wb = world_center_(col_b, pos_b, rot_b);
    phys_quat_t rb = world_rot_(col_b, rot_b);

    /* Cheap bounding sphere pre-filter. */
    float br_a = bounding_radius_(ctx, col_a);
    float br_b = bounding_radius_(ctx, col_b);
    float dx = wb.x - wa.x, dy = wb.y - wa.y, dz = wb.z - wa.z;
    float dist_sq = dx * dx + dy * dy + dz * dz;
    float sum_r = br_a + br_b;
    if (dist_sq > sum_r * sum_r) return false;

    /* Order by type so we only handle upper-triangle of type matrix. */
    if (col_a->type <= col_b->type) {
        return overlap_ordered_(ctx, col_a, wa, ra, col_b, wb, rb);
    } else {
        return overlap_ordered_(ctx, col_b, wb, rb, col_a, wa, ra);
    }
}
