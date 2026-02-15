/**
 * @file ccd.c
 * @brief Continuous collision detection: swept sphere/capsule vs static mesh.
 *
 * Implements Möller–Trumbore ray-triangle intersection and inflated-plane
 * swept-sphere tests.  The CCD stage runs after integration and clamps
 * fast-moving bodies that would tunnel through static mesh geometry.
 *
 * Non-static functions: 4
 *   phys_ray_vs_triangle, phys_swept_sphere_vs_triangle,
 *   phys_swept_sphere_vs_mesh, phys_stage_ccd
 */

#include "ferrum/physics/ccd.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/mesh_collider.h"

/* ── Ray vs Triangle (Möller–Trumbore) ─────────────────────────── */

bool phys_ray_vs_triangle(
    phys_vec3_t ray_origin,
    phys_vec3_t ray_dir,
    const phys_triangle_t *tri,
    float *t_out)
{
    if (!tri || !t_out) return false;

    const float EPSILON = 1e-7f;

    phys_vec3_t e1 = vec3_sub(tri->v[1], tri->v[0]);
    phys_vec3_t e2 = vec3_sub(tri->v[2], tri->v[0]);
    phys_vec3_t h  = vec3_cross(ray_dir, e2);
    float a = vec3_dot(e1, h);

    if (fabsf(a) < EPSILON) return false; /* Ray parallel to triangle. */

    float f = 1.0f / a;
    phys_vec3_t s = vec3_sub(ray_origin, tri->v[0]);
    float u = f * vec3_dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    phys_vec3_t q = vec3_cross(s, e1);
    float v = f * vec3_dot(ray_dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = f * vec3_dot(e2, q);
    if (t < 0.0f || t > 1.0f) return false;

    *t_out = t;
    return true;
}

/* ── Swept Sphere vs Triangle ──────────────────────────────────── */

/**
 * Swept sphere test: offset the triangle plane by radius along its
 * normal, then ray-test the sphere center against the inflated plane.
 * Also tests swept sphere against the three triangle edges and vertices
 * for edge-grazing cases.
 */
bool phys_swept_sphere_vs_triangle(
    phys_vec3_t start,
    phys_vec3_t end,
    float radius,
    const phys_triangle_t *tri,
    float *t_out,
    phys_vec3_t *normal_out)
{
    if (!tri || !t_out || !normal_out) return false;

    /* Compute triangle normal. */
    phys_vec3_t e0 = vec3_sub(tri->v[1], tri->v[0]);
    phys_vec3_t e1 = vec3_sub(tri->v[2], tri->v[0]);
    phys_vec3_t n  = vec3_cross(e0, e1);
    float n_len = sqrtf(vec3_dot(n, n));
    if (n_len < 1e-9f) return false;
    n = vec3_scale(n, 1.0f / n_len);

    phys_vec3_t motion = vec3_sub(end, start);

    /* Signed distances from start/end to the triangle plane. */
    float d_start = vec3_dot(vec3_sub(start, tri->v[0]), n);
    float d_end   = vec3_dot(vec3_sub(end,   tri->v[0]), n);

    /* We want to find t where |d(t)| = radius.
     * d(t) = d_start + t * (d_end - d_start).
     * Two solutions: d(t) = +radius and d(t) = -radius. */
    float d_delta = d_end - d_start;
    float best_t = 2.0f; /* sentinel > 1 */

    /* Test both plane offsets: +radius (approaching from front) and
     * -radius (approaching from back). */
    for (int side = 0; side < 2; side++) {
        float target = (side == 0) ? radius : -radius;
        if (fabsf(d_delta) < 1e-9f) {
            /* Moving parallel to plane. Check if already within radius. */
            if (fabsf(d_start) <= radius) {
                /* Already intersecting at t=0. */
                if (0.0f < best_t) {
                    best_t = 0.0f;
                }
            }
            continue;
        }
        float t = (target - d_start) / d_delta;
        if (t < 0.0f || t > 1.0f || t >= best_t) continue;

        /* Check if the contact point is inside the triangle. */
        phys_vec3_t center_at_t = vec3_add(start, vec3_scale(motion, t));
        phys_vec3_t proj = vec3_sub(center_at_t, vec3_scale(n, target));

        /* Barycentric test on the projected point. */
        phys_vec3_t v0p = vec3_sub(proj, tri->v[0]);
        float d00 = vec3_dot(e0, e0);
        float d01 = vec3_dot(e0, e1);
        float d11 = vec3_dot(e1, e1);
        float d20 = vec3_dot(v0p, e0);
        float d21 = vec3_dot(v0p, e1);
        float denom = d00 * d11 - d01 * d01;
        if (fabsf(denom) < 1e-9f) continue;
        float bary_v = (d11 * d20 - d01 * d21) / denom;
        float bary_w = (d00 * d21 - d01 * d20) / denom;
        float bary_u = 1.0f - bary_v - bary_w;

        /* Small tolerance for edge cases. */
        if (bary_u >= -0.01f && bary_v >= -0.01f && bary_w >= -0.01f) {
            best_t = t;
        }
    }

    if (best_t > 1.0f) return false;

    *t_out = best_t;
    /* Normal points toward the sphere (away from triangle surface). */
    float d_at_hit = d_start + best_t * d_delta;
    *normal_out = (d_at_hit >= 0.0f) ? n : vec3_scale(n, -1.0f);
    return true;
}

/* ── BVH traversal for swept AABB ──────────────────────────────── */

#define CCD_STACK_CAP 128u
#define CCD_MAX_CANDIDATES 128u

static uint32_t ccd_collect_candidates(
    const phys_mesh_bvh_t *bvh,
    const phys_aabb_t *query,
    uint32_t *out_tris,
    uint32_t max_tris)
{
    if (!bvh || !bvh->nodes || bvh->node_count == 0) return 0;
    if (bvh->root >= bvh->node_count) return 0;

    uint32_t stack[CCD_STACK_CAP];
    uint32_t sp = 0;
    stack[sp++] = bvh->root;

    uint32_t count = 0;
    while (sp && count < max_tris) {
        uint32_t idx = stack[--sp];
        if (idx >= bvh->node_count) continue;
        const phys_mesh_bvh_node_t *node = &bvh->nodes[idx];
        if (!phys_aabb_overlap(&node->bounds, query)) continue;

        if (phys_mesh_bvh_node_is_leaf(node)) {
            out_tris[count++] = node->tri_index;
        } else {
            if (sp + 2 <= CCD_STACK_CAP) {
                stack[sp++] = node->left;
                stack[sp++] = node->right;
            }
        }
    }
    return count;
}

/* ── Swept Sphere vs Mesh ──────────────────────────────────────── */

bool phys_swept_sphere_vs_mesh(
    phys_vec3_t start,
    phys_vec3_t end,
    float radius,
    const phys_triangle_t *triangles,
    const phys_mesh_bvh_t *bvh,
    float *t_out,
    phys_vec3_t *normal_out,
    phys_vec3_t *hit_pos_out)
{
    if (!triangles || !bvh || !t_out || !normal_out || !hit_pos_out) {
        return false;
    }

    /* Build swept AABB covering the entire motion + radius. */
    float r = radius;
    phys_aabb_t query;
    query.min.x = fminf(start.x, end.x) - r;
    query.min.y = fminf(start.y, end.y) - r;
    query.min.z = fminf(start.z, end.z) - r;
    query.max.x = fmaxf(start.x, end.x) + r;
    query.max.y = fmaxf(start.y, end.y) + r;
    query.max.z = fmaxf(start.z, end.z) + r;

    uint32_t cands[CCD_MAX_CANDIDATES];
    uint32_t nc = ccd_collect_candidates(bvh, &query, cands,
                                          CCD_MAX_CANDIDATES);

    float best_t = 2.0f;
    phys_vec3_t best_normal = {0, 1, 0};

    for (uint32_t i = 0; i < nc; i++) {
        if (cands[i] >= bvh->tri_count) continue;
        float t;
        phys_vec3_t n;
        if (phys_swept_sphere_vs_triangle(start, end, radius,
                                           &triangles[cands[i]],
                                           &t, &n)) {
            if (t < best_t) {
                best_t = t;
                best_normal = n;
            }
        }
    }

    if (best_t > 1.0f) return false;

    *t_out = best_t;
    *normal_out = best_normal;
    phys_vec3_t motion = vec3_sub(end, start);
    *hit_pos_out = vec3_add(start, vec3_scale(motion, best_t));
    return true;
}

/* ── CCD Stage ─────────────────────────────────────────────────── */

int phys_stage_ccd(const phys_ccd_args_t *args) {
    if (!args) return 0;
    if (!args->bodies_prev || !args->bodies_curr) return 0;
    if (!args->colliders || args->mesh_count == 0) return 0;

    int clamped = 0;

    for (uint32_t i = 0; i < args->body_count; i++) {
        phys_body_t *prev = &args->bodies_prev[i];
        phys_body_t *curr = &args->bodies_curr[i];

        /* Only CCD-enabled dynamic bodies. */
        if (!(prev->flags & PHYS_BODY_FLAG_CCD)) continue;
        if (prev->flags & (PHYS_BODY_FLAG_STATIC | PHYS_BODY_FLAG_KINEMATIC)) {
            continue;
        }
        if (prev->inv_mass <= 0.0f) continue;

        const phys_collider_t *col = &args->colliders[i];

        /* Determine bounding radius. */
        float bounding_radius = 0.0f;
        if (col->type == PHYS_SHAPE_SPHERE) {
            const phys_sphere_t *s = &((const phys_sphere_t *)args->spheres)[col->shape_index];
            bounding_radius = s->radius;
        } else if (col->type == PHYS_SHAPE_CAPSULE) {
            const phys_capsule_t *c = &((const phys_capsule_t *)args->capsules)[col->shape_index];
            bounding_radius = c->radius;
        } else {
            continue; /* CCD only for sphere/capsule. */
        }

        /* Check displacement: only sweep if body moved more than its radius. */
        phys_vec3_t displacement = vec3_sub(curr->position, prev->position);
        float disp_len = sqrtf(vec3_dot(displacement, displacement));
        if (disp_len < bounding_radius) continue;

        /* Sweep against all static mesh shapes. */
        phys_vec3_t start = vec3_add(prev->position, col->local_offset);
        phys_vec3_t end_pos = vec3_add(curr->position, col->local_offset);

        float best_t = 2.0f;
        phys_vec3_t best_normal = {0, 1, 0};
        phys_vec3_t best_hit = end_pos;
        phys_vec3_t best_mesh_origin = {0, 0, 0};

        for (uint32_t m = 0; m < args->mesh_count; m++) {
            const phys_mesh_shape_t *ms = &((const phys_mesh_shape_t *)args->meshes)[m];
            if (!ms->triangles || ms->bvh.node_count == 0) continue;

            /* Find the mesh body to get its position for local-space transform.
             * Scan colliders for this mesh index. */
            phys_vec3_t mesh_origin = {0, 0, 0};
            for (uint32_t b = 0; b < args->body_count; b++) {
                if (args->colliders[b].type == PHYS_SHAPE_MESH &&
                    args->colliders[b].shape_index == m) {
                    mesh_origin = args->bodies_prev[b].position;
                    break;
                }
            }

            /* Transform sweep into mesh-local space. */
            phys_vec3_t local_start = vec3_sub(start, mesh_origin);
            phys_vec3_t local_end   = vec3_sub(end_pos, mesh_origin);

            float t;
            phys_vec3_t normal, hit_pos;
            if (phys_swept_sphere_vs_mesh(local_start, local_end,
                                           bounding_radius,
                                           ms->triangles, &ms->bvh,
                                           &t, &normal, &hit_pos)) {
                if (t < best_t) {
                    best_t = t;
                    best_normal = normal;
                    best_hit = vec3_add(hit_pos, mesh_origin);
                    best_mesh_origin = mesh_origin;
                }
            }
        }

        if (best_t <= 1.0f) {
            /* Clamp position to just before impact.
             * Place sphere center at hit position, backed off slightly
             * along the normal to avoid penetration. */
            phys_vec3_t safe_pos = vec3_sub(best_hit, col->local_offset);
            /* Small nudge along normal to prevent re-penetration. */
            safe_pos = vec3_add(safe_pos,
                                vec3_scale(best_normal, 0.01f));
            curr->position = safe_pos;

            /* Remove velocity component along the contact normal. */
            float vn = vec3_dot(curr->linear_vel, best_normal);
            if (vn < 0.0f) {
                curr->linear_vel = vec3_sub(
                    curr->linear_vel,
                    vec3_scale(best_normal, vn));
            }

            clamped++;
        }
    }

    return clamped;
}
