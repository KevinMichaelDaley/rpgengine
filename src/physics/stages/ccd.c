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

/* ── Helpers for edge/vertex swept-sphere tests ────────────────── */

/**
 * Solve the quadratic a*t^2 + b*t + c = 0 for the smallest root
 * in [0, cap).  Returns false if no valid root exists.
 */
static bool solve_quadratic_smallest(float a, float b, float c,
                                     float cap, float *t_out)
{
    if (fabsf(a) < 1e-12f) {
        /* Degenerate: linear. */
        if (fabsf(b) < 1e-12f) return false;
        float t = -c / b;
        if (t >= 0.0f && t < cap) { *t_out = t; return true; }
        return false;
    }
    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;
    float sq = sqrtf(disc);
    float inv2a = 1.0f / (2.0f * a);
    float t0 = (-b - sq) * inv2a;
    float t1 = (-b + sq) * inv2a;
    if (t0 >= 0.0f && t0 < cap) { *t_out = t0; return true; }
    if (t1 >= 0.0f && t1 < cap) { *t_out = t1; return true; }
    return false;
}

/**
 * Swept sphere vs single vertex: find earliest t where
 * |center(t) - vertex|^2 = radius^2.
 * center(t) = start + t * motion.
 */
static bool swept_sphere_vs_vertex(
    phys_vec3_t start, phys_vec3_t motion,
    phys_vec3_t vertex, float radius,
    float cap, float *t_out)
{
    phys_vec3_t m = vec3_sub(start, vertex);
    float a = vec3_dot(motion, motion);
    float b = 2.0f * vec3_dot(m, motion);
    float c = vec3_dot(m, m) - radius * radius;
    return solve_quadratic_smallest(a, b, c, cap, t_out);
}

/**
 * Swept sphere vs edge segment (v0→v1): find earliest t where
 * distance from center(t) to the line segment equals radius,
 * AND the projection along the edge is within [0, 1].
 *
 * We decompose into components along and perpendicular to the edge.
 * The perpendicular distance squared = radius^2 gives a quadratic in t.
 */
static bool swept_sphere_vs_edge(
    phys_vec3_t start, phys_vec3_t motion,
    phys_vec3_t v0, phys_vec3_t v1,
    float radius, float cap, float *t_out)
{
    phys_vec3_t edge = vec3_sub(v1, v0);
    float edge_sq = vec3_dot(edge, edge);
    if (edge_sq < 1e-12f) {
        return swept_sphere_vs_vertex(start, motion, v0, radius, cap, t_out);
    }
    float inv_edge_sq = 1.0f / edge_sq;

    phys_vec3_t m = vec3_sub(start, v0);
    float m_dot_e = vec3_dot(m, edge);
    float d_dot_e = vec3_dot(motion, edge);

    /* Perpendicular components of m and motion relative to edge. */
    phys_vec3_t m_perp = vec3_sub(m, vec3_scale(edge, m_dot_e * inv_edge_sq));
    phys_vec3_t d_perp = vec3_sub(motion, vec3_scale(edge, d_dot_e * inv_edge_sq));

    float a = vec3_dot(d_perp, d_perp);
    float b = 2.0f * vec3_dot(m_perp, d_perp);
    float c = vec3_dot(m_perp, m_perp) - radius * radius;

    float t;
    if (!solve_quadratic_smallest(a, b, c, cap, &t)) return false;

    /* Verify contact point projects within the edge segment [0, 1]. */
    float s = (m_dot_e + t * d_dot_e) * inv_edge_sq;
    if (s < 0.0f || s > 1.0f) return false;

    *t_out = t;
    return true;
}

/**
 * Swept sphere vs single triangle — full feature test.
 *
 * Tests the face (inflated plane), the 3 edges (cylinder test),
 * and the 3 vertices (sphere test).  Returns the earliest TOI.
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
    float d_delta = d_end - d_start;

    float best_t = 1.0f + 1e-5f; /* sentinel > 1 */
    phys_vec3_t best_normal = n;

    /* ── Face test: find t where signed distance = ±radius ───── */
    for (int side = 0; side < 2; side++) {
        float target = (side == 0) ? radius : -radius;
        if (fabsf(d_delta) < 1e-9f) {
            if (fabsf(d_start) <= radius && 0.0f < best_t) {
                /* Already within radius — check face containment. */
                phys_vec3_t proj = vec3_sub(start, vec3_scale(n, d_start));
                phys_vec3_t v0p = vec3_sub(proj, tri->v[0]);
                float d00 = vec3_dot(e0, e0), d01 = vec3_dot(e0, e1);
                float d11 = vec3_dot(e1, e1);
                float d20 = vec3_dot(v0p, e0), d21 = vec3_dot(v0p, e1);
                float denom = d00 * d11 - d01 * d01;
                if (fabsf(denom) > 1e-9f) {
                    float bv = (d11 * d20 - d01 * d21) / denom;
                    float bw = (d00 * d21 - d01 * d20) / denom;
                    float bu = 1.0f - bv - bw;
                    if (bu >= -0.01f && bv >= -0.01f && bw >= -0.01f) {
                        best_t = 0.0f;
                        best_normal = (d_start >= 0.0f) ? n : vec3_scale(n, -1.0f);
                    }
                }
            }
            continue;
        }
        float t = (target - d_start) / d_delta;
        if (t < 0.0f || t > 1.0f || t >= best_t) continue;

        phys_vec3_t center_at_t = vec3_add(start, vec3_scale(motion, t));
        phys_vec3_t proj = vec3_sub(center_at_t, vec3_scale(n, target));

        phys_vec3_t v0p = vec3_sub(proj, tri->v[0]);
        float d00 = vec3_dot(e0, e0), d01 = vec3_dot(e0, e1);
        float d11 = vec3_dot(e1, e1);
        float d20 = vec3_dot(v0p, e0), d21 = vec3_dot(v0p, e1);
        float denom = d00 * d11 - d01 * d01;
        if (fabsf(denom) < 1e-9f) continue;
        float bary_v = (d11 * d20 - d01 * d21) / denom;
        float bary_w = (d00 * d21 - d01 * d20) / denom;
        float bary_u = 1.0f - bary_v - bary_w;

        if (bary_u >= -0.01f && bary_v >= -0.01f && bary_w >= -0.01f) {
            best_t = t;
            float d_at_hit = d_start + t * d_delta;
            best_normal = (d_at_hit >= 0.0f) ? n : vec3_scale(n, -1.0f);
        }
    }

    /* ── Edge tests: swept sphere vs each of the 3 edges ─────── */
    static const int edge_idx[3][2] = {{0,1}, {1,2}, {2,0}};
    for (int ei = 0; ei < 3; ei++) {
        float t;
        if (swept_sphere_vs_edge(start, motion,
                                  tri->v[edge_idx[ei][0]],
                                  tri->v[edge_idx[ei][1]],
                                  radius, best_t, &t)) {
            best_t = t;
            /* Normal: from closest point on edge toward sphere center. */
            phys_vec3_t center_at_t = vec3_add(start, vec3_scale(motion, t));
            phys_vec3_t ev = vec3_sub(tri->v[edge_idx[ei][1]],
                                       tri->v[edge_idx[ei][0]]);
            float ev_sq = vec3_dot(ev, ev);
            phys_vec3_t cv = vec3_sub(center_at_t, tri->v[edge_idx[ei][0]]);
            float s = vec3_dot(cv, ev) / ev_sq;
            if (s < 0.0f) s = 0.0f;
            if (s > 1.0f) s = 1.0f;
            phys_vec3_t closest = vec3_add(tri->v[edge_idx[ei][0]],
                                            vec3_scale(ev, s));
            phys_vec3_t diff = vec3_sub(center_at_t, closest);
            float diff_len = sqrtf(vec3_dot(diff, diff));
            best_normal = (diff_len > 1e-9f)
                ? vec3_scale(diff, 1.0f / diff_len) : n;
        }
    }

    /* ── Vertex tests: swept sphere vs each of the 3 vertices ── */
    for (int vi = 0; vi < 3; vi++) {
        float t;
        if (swept_sphere_vs_vertex(start, motion, tri->v[vi],
                                    radius, best_t, &t)) {
            best_t = t;
            phys_vec3_t center_at_t = vec3_add(start, vec3_scale(motion, t));
            phys_vec3_t diff = vec3_sub(center_at_t, tri->v[vi]);
            float diff_len = sqrtf(vec3_dot(diff, diff));
            best_normal = (diff_len > 1e-9f)
                ? vec3_scale(diff, 1.0f / diff_len) : n;
        }
    }

    if (best_t > 1.0f) return false;

    *t_out = best_t;
    *normal_out = best_normal;
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
