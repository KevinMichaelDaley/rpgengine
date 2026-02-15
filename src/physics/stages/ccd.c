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
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/phys_pool.h"

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
#define CCD_MAX_CANDIDATES 512u

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

/** Rotate a vector by a quaternion: q * v * q^-1. */
static phys_vec3_t ccd_quat_rotate(phys_quat_t q, phys_vec3_t v) {
    phys_vec3_t u = {q.x, q.y, q.z};
    float s = q.w;
    phys_vec3_t t = vec3_scale(vec3_cross(u, v), 2.0f);
    return vec3_add(v, vec3_add(vec3_scale(t, s), vec3_cross(u, t)));
}

/** Multiply two quaternions. */
static phys_quat_t ccd_quat_mul(phys_quat_t a, phys_quat_t b) {
    return (phys_quat_t){
        .x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        .y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        .z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

/* ── Closest point on triangle to a point ──────────────────────── */

/**
 * Returns the closest point on triangle (a,b,c) to point p,
 * along with the outward normal at that point.
 */
static phys_vec3_t closest_point_on_triangle(
    phys_vec3_t p, const phys_triangle_t *tri, phys_vec3_t *normal_out)
{
    phys_vec3_t a = tri->v[0], b = tri->v[1], c = tri->v[2];
    phys_vec3_t ab = vec3_sub(b, a);
    phys_vec3_t ac = vec3_sub(c, a);
    phys_vec3_t ap = vec3_sub(p, a);

    float d1 = vec3_dot(ab, ap);
    float d2 = vec3_dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        /* Closest to vertex A. */
        phys_vec3_t n = vec3_cross(ab, ac);
        float nl = sqrtf(vec3_dot(n, n));
        *normal_out = (nl > 1e-9f) ? vec3_scale(n, 1.0f / nl)
                                    : (phys_vec3_t){0, 1, 0};
        return a;
    }

    phys_vec3_t bp = vec3_sub(p, b);
    float d3 = vec3_dot(ab, bp);
    float d4 = vec3_dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        phys_vec3_t n = vec3_cross(ab, ac);
        float nl = sqrtf(vec3_dot(n, n));
        *normal_out = (nl > 1e-9f) ? vec3_scale(n, 1.0f / nl)
                                    : (phys_vec3_t){0, 1, 0};
        return b;
    }

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        phys_vec3_t n = vec3_cross(ab, ac);
        float nl = sqrtf(vec3_dot(n, n));
        *normal_out = (nl > 1e-9f) ? vec3_scale(n, 1.0f / nl)
                                    : (phys_vec3_t){0, 1, 0};
        return vec3_add(a, vec3_scale(ab, v));
    }

    phys_vec3_t cp = vec3_sub(p, c);
    float d5 = vec3_dot(ab, cp);
    float d6 = vec3_dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        phys_vec3_t n = vec3_cross(ab, ac);
        float nl = sqrtf(vec3_dot(n, n));
        *normal_out = (nl > 1e-9f) ? vec3_scale(n, 1.0f / nl)
                                    : (phys_vec3_t){0, 1, 0};
        return c;
    }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        phys_vec3_t n = vec3_cross(ab, ac);
        float nl = sqrtf(vec3_dot(n, n));
        *normal_out = (nl > 1e-9f) ? vec3_scale(n, 1.0f / nl)
                                    : (phys_vec3_t){0, 1, 0};
        return vec3_add(a, vec3_scale(ac, w));
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        phys_vec3_t n = vec3_cross(ab, ac);
        float nl = sqrtf(vec3_dot(n, n));
        *normal_out = (nl > 1e-9f) ? vec3_scale(n, 1.0f / nl)
                                    : (phys_vec3_t){0, 1, 0};
        return vec3_add(b, vec3_scale(vec3_sub(c, b), w));
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    phys_vec3_t n = vec3_cross(ab, ac);
    float nl = sqrtf(vec3_dot(n, n));
    *normal_out = (nl > 1e-9f) ? vec3_scale(n, 1.0f / nl)
                                : (phys_vec3_t){0, 1, 0};
    return vec3_add(a, vec3_add(vec3_scale(ab, v), vec3_scale(ac, w)));
}

/* ── Sphere-in-mesh penetration test ───────────────────────────── */

/**
 * Check if a sphere at @p center with @p radius is penetrating any
 * triangle in the BVH.  If so, return the deepest penetration depth
 * and a push-out normal (pointing away from the triangle surface).
 */
static bool ccd_sphere_penetrating_mesh(
    phys_vec3_t center, float radius,
    const phys_triangle_t *triangles,
    const phys_mesh_bvh_t *bvh,
    float *depth_out, phys_vec3_t *normal_out)
{
    /* Build a static AABB around the sphere. */
    phys_aabb_t query;
    query.min.x = center.x - radius;
    query.min.y = center.y - radius;
    query.min.z = center.z - radius;
    query.max.x = center.x + radius;
    query.max.y = center.y + radius;
    query.max.z = center.z + radius;

    uint32_t cands[CCD_MAX_CANDIDATES];
    uint32_t nc = ccd_collect_candidates(bvh, &query, cands,
                                          CCD_MAX_CANDIDATES);

    float deepest = 0.0f;
    phys_vec3_t best_normal = {0, 1, 0};
    bool found = false;

    for (uint32_t i = 0; i < nc; i++) {
        if (cands[i] >= bvh->tri_count) continue;
        const phys_triangle_t *tri = &triangles[cands[i]];

        phys_vec3_t tri_normal;
        phys_vec3_t closest = closest_point_on_triangle(center, tri,
                                                         &tri_normal);
        phys_vec3_t diff = vec3_sub(center, closest);
        float dist_sq = vec3_dot(diff, diff);

        if (dist_sq < radius * radius) {
            float dist = sqrtf(dist_sq);
            float pen = radius - dist;

            /* Push-out direction: from closest point toward sphere center.
             * If dist is ~0 (center is ON the triangle), use face normal. */
            phys_vec3_t push;
            if (dist > 1e-6f) {
                push = vec3_scale(diff, 1.0f / dist);
            } else {
                push = tri_normal;
            }

            if (pen > deepest) {
                deepest = pen;
                best_normal = push;
                found = true;
            }
        }
    }

    if (found) {
        *depth_out = deepest;
        *normal_out = best_normal;
    }
    return found;
}

/**
 * Check penetration of a sphere against all mesh shapes, returning
 * deepest penetration and push-out direction.
 */
static bool ccd_depenetrate_sphere_vs_meshes(
    phys_vec3_t center, float radius,
    const phys_ccd_args_t *args,
    float *depth_out, phys_vec3_t *normal_out)
{
    float deepest = 0.0f;
    phys_vec3_t best_normal = {0, 1, 0};
    bool any = false;

    for (uint32_t m = 0; m < args->mesh_count; m++) {
        const phys_mesh_shape_t *ms =
            &((const phys_mesh_shape_t *)args->meshes)[m];
        if (!ms->triangles || ms->bvh.node_count == 0) continue;

        phys_vec3_t mesh_origin = {0, 0, 0};
        for (uint32_t b = 0; b < args->body_count; b++) {
            if (args->colliders[b].type == PHYS_SHAPE_MESH &&
                args->colliders[b].shape_index == m) {
                mesh_origin = args->bodies_prev[b].position;
                break;
            }
        }

        phys_vec3_t local_center = vec3_sub(center, mesh_origin);
        float depth;
        phys_vec3_t normal;
        if (ccd_sphere_penetrating_mesh(local_center, radius,
                                         ms->triangles, &ms->bvh,
                                         &depth, &normal)) {
            if (depth > deepest) {
                deepest = depth;
                best_normal = normal;
                any = true;
            }
        }
    }

    if (any) {
        *depth_out = deepest;
        *normal_out = best_normal;
    }
    return any;
}

/* ── Distance from point to line segment ───────────────────────── */

/**
 * Closest point on segment (seg_a, seg_b) to point p.
 * Returns the closest point and optionally the segment parameter in [0,1].
 */
static phys_vec3_t closest_point_on_segment(
    phys_vec3_t p, phys_vec3_t seg_a, phys_vec3_t seg_b, float *param_out)
{
    phys_vec3_t ab = vec3_sub(seg_b, seg_a);
    float ab_sq = vec3_dot(ab, ab);
    if (ab_sq < 1e-12f) {
        if (param_out) *param_out = 0.0f;
        return seg_a;
    }
    float t = vec3_dot(vec3_sub(p, seg_a), ab) / ab_sq;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (param_out) *param_out = t;
    return vec3_add(seg_a, vec3_scale(ab, t));
}

/* ── Capsule SDF vs triangle ───────────────────────────────────── */

/**
 * Compute the signed distance from a capsule (axis segment ep_a→ep_b,
 * with given radius) to a triangle.  Returns the penetration depth
 * (positive = penetrating) and the push-out normal.
 *
 * The capsule SDF at point P is: dist(P, segment) - radius.
 * We need the minimum distance from the triangle surface to the
 * capsule segment, minus radius.
 *
 * We test: each triangle vertex vs capsule segment, each triangle
 * edge vs capsule segment (segment-segment distance), and the
 * capsule endpoints vs triangle face (point-triangle distance).
 */
static float capsule_vs_triangle_depth(
    phys_vec3_t ep_a, phys_vec3_t ep_b, float radius,
    const phys_triangle_t *tri, phys_vec3_t *normal_out)
{
    float best_dist_sq = 1e18f;
    phys_vec3_t best_closest_on_tri = tri->v[0];
    phys_vec3_t best_closest_on_cap = ep_a;

    /* Test each triangle vertex against the capsule segment. */
    for (int vi = 0; vi < 3; vi++) {
        phys_vec3_t cp = closest_point_on_segment(
            tri->v[vi], ep_a, ep_b, NULL);
        phys_vec3_t diff = vec3_sub(tri->v[vi], cp);
        float d2 = vec3_dot(diff, diff);
        if (d2 < best_dist_sq) {
            best_dist_sq = d2;
            best_closest_on_tri = tri->v[vi];
            best_closest_on_cap = cp;
        }
    }

    /* Test each capsule endpoint against the triangle. */
    phys_vec3_t cap_pts[2] = {ep_a, ep_b};
    for (int ci = 0; ci < 2; ci++) {
        phys_vec3_t tri_normal;
        phys_vec3_t cp = closest_point_on_triangle(
            cap_pts[ci], tri, &tri_normal);
        phys_vec3_t diff = vec3_sub(cap_pts[ci], cp);
        float d2 = vec3_dot(diff, diff);
        if (d2 < best_dist_sq) {
            best_dist_sq = d2;
            best_closest_on_tri = cp;
            best_closest_on_cap = cap_pts[ci];
        }
    }

    /* Test each triangle edge against the capsule segment.
     * Segment-segment closest distance. */
    static const int edge_idx[3][2] = {{0,1}, {1,2}, {2,0}};
    for (int ei = 0; ei < 3; ei++) {
        phys_vec3_t e0 = tri->v[edge_idx[ei][0]];
        phys_vec3_t e1 = tri->v[edge_idx[ei][1]];

        /* Closest points between two segments:
         * seg1: ep_a + s*(ep_b - ep_a), s in [0,1]
         * seg2: e0   + t*(e1  - e0),    t in [0,1]
         * We use the standard segment-segment algorithm. */
        phys_vec3_t d1 = vec3_sub(ep_b, ep_a);
        phys_vec3_t d2 = vec3_sub(e1, e0);
        phys_vec3_t r  = vec3_sub(ep_a, e0);

        float a = vec3_dot(d1, d1);
        float e = vec3_dot(d2, d2);
        float f = vec3_dot(d2, r);

        float s_param, t_param;

        if (a < 1e-12f && e < 1e-12f) {
            /* Both degenerate to points. */
            s_param = 0.0f;
            t_param = 0.0f;
        } else if (a < 1e-12f) {
            s_param = 0.0f;
            t_param = f / e;
            if (t_param < 0.0f) t_param = 0.0f;
            if (t_param > 1.0f) t_param = 1.0f;
        } else {
            float c = vec3_dot(d1, r);
            if (e < 1e-12f) {
                t_param = 0.0f;
                s_param = -c / a;
                if (s_param < 0.0f) s_param = 0.0f;
                if (s_param > 1.0f) s_param = 1.0f;
            } else {
                float b = vec3_dot(d1, d2);
                float denom = a * e - b * b;
                if (fabsf(denom) > 1e-12f) {
                    s_param = (b * f - c * e) / denom;
                    if (s_param < 0.0f) s_param = 0.0f;
                    if (s_param > 1.0f) s_param = 1.0f;
                } else {
                    s_param = 0.0f;
                }
                t_param = (b * s_param + f) / e;
                if (t_param < 0.0f) {
                    t_param = 0.0f;
                    s_param = -c / a;
                    if (s_param < 0.0f) s_param = 0.0f;
                    if (s_param > 1.0f) s_param = 1.0f;
                } else if (t_param > 1.0f) {
                    t_param = 1.0f;
                    s_param = (b - c) / a;
                    if (s_param < 0.0f) s_param = 0.0f;
                    if (s_param > 1.0f) s_param = 1.0f;
                }
            }
        }

        phys_vec3_t p1 = vec3_add(ep_a, vec3_scale(d1, s_param));
        phys_vec3_t p2 = vec3_add(e0,   vec3_scale(d2, t_param));
        phys_vec3_t diff = vec3_sub(p1, p2);
        float d2_val = vec3_dot(diff, diff);
        if (d2_val < best_dist_sq) {
            best_dist_sq = d2_val;
            best_closest_on_cap = p1;
            best_closest_on_tri = p2;
        }
    }

    float dist = sqrtf(best_dist_sq);
    float pen = radius - dist;

    if (pen > 0.0f && normal_out) {
        phys_vec3_t diff = vec3_sub(best_closest_on_cap, best_closest_on_tri);
        float diff_len = sqrtf(vec3_dot(diff, diff));
        if (diff_len > 1e-9f) {
            *normal_out = vec3_scale(diff, 1.0f / diff_len);
        } else {
            /* Fallback: use triangle face normal. */
            phys_vec3_t e0 = vec3_sub(tri->v[1], tri->v[0]);
            phys_vec3_t e1 = vec3_sub(tri->v[2], tri->v[0]);
            phys_vec3_t fn = vec3_cross(e0, e1);
            float fn_len = sqrtf(vec3_dot(fn, fn));
            *normal_out = (fn_len > 1e-9f)
                ? vec3_scale(fn, 1.0f / fn_len)
                : (phys_vec3_t){0, 1, 0};
        }
    }

    return pen;
}

/* ── Quaternion slerp ──────────────────────────────────────────── */

/** Spherical linear interpolation between two quaternions. */
static phys_quat_t ccd_quat_slerp(phys_quat_t a, phys_quat_t b, float t) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    /* Ensure shortest path. */
    if (dot < 0.0f) {
        b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
        dot = -dot;
    }
    if (dot > 0.9995f) {
        /* Close enough — lerp + normalize. */
        phys_quat_t r = {
            .x = a.x + t * (b.x - a.x),
            .y = a.y + t * (b.y - a.y),
            .z = a.z + t * (b.z - a.z),
            .w = a.w + t * (b.w - a.w),
        };
        float len = sqrtf(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
        if (len > 1e-9f) {
            float inv = 1.0f / len;
            r.x *= inv; r.y *= inv; r.z *= inv; r.w *= inv;
        }
        return r;
    }
    float theta = acosf(dot);
    float sin_theta = sinf(theta);
    float wa = sinf((1.0f - t) * theta) / sin_theta;
    float wb = sinf(t * theta) / sin_theta;
    return (phys_quat_t){
        .x = wa * a.x + wb * b.x,
        .y = wa * a.y + wb * b.y,
        .z = wa * a.z + wb * b.z,
        .w = wa * a.w + wb * b.w,
    };
}

/* ── Swept capsule vs mesh (discrete SDF union) ────────────────── */

/** Maximum number of subsample positions for swept capsule. */
#define CCD_MAX_SUBSAMPLES 16

/**
 * Sweep a capsule (defined by body center, collider offset, orientation,
 * half_height, radius) from prev to curr state, testing against all
 * meshes.  Uses discrete subsampling: at each sample the capsule SDF
 * is evaluated against candidate triangles; the union (min) across
 * samples detects penetration.
 *
 * Returns true if any sample penetrates, with earliest sample index
 * mapped back to a parametric t in [0, 1].
 */
static bool ccd_swept_capsule_vs_meshes(
    const phys_body_t *prev, const phys_body_t *curr,
    const phys_collider_t *col, float radius, float half_h,
    const phys_ccd_args_t *args, float margin,
    float *best_t_out, phys_vec3_t *best_normal_out)
{
    /* Compute total displacement to decide subsample count. */
    phys_vec3_t displacement = vec3_sub(curr->position, prev->position);
    float disp_len = sqrtf(vec3_dot(displacement, displacement));

    /* Number of subsamples: at least 4, scale with displacement/radius. */
    int n_samples = (int)(disp_len / radius) + 4;
    if (n_samples > CCD_MAX_SUBSAMPLES) n_samples = CCD_MAX_SUBSAMPLES;
    if (n_samples < 4) n_samples = 4;

    /* Build a conservative AABB covering the full swept volume. */
    float sweep_radius = radius + half_h + margin;
    phys_aabb_t swept_aabb;
    swept_aabb.min.x = fminf(prev->position.x, curr->position.x) - sweep_radius;
    swept_aabb.min.y = fminf(prev->position.y, curr->position.y) - sweep_radius;
    swept_aabb.min.z = fminf(prev->position.z, curr->position.z) - sweep_radius;
    swept_aabb.max.x = fmaxf(prev->position.x, curr->position.x) + sweep_radius;
    swept_aabb.max.y = fmaxf(prev->position.y, curr->position.y) + sweep_radius;
    swept_aabb.max.z = fmaxf(prev->position.z, curr->position.z) + sweep_radius;

    bool any_hit = false;
    float best_t = 2.0f;
    phys_vec3_t best_normal = {0, 1, 0};

    for (uint32_t m = 0; m < args->mesh_count; m++) {
        const phys_mesh_shape_t *ms =
            &((const phys_mesh_shape_t *)args->meshes)[m];
        if (!ms->triangles || ms->bvh.node_count == 0) continue;

        /* Find mesh body position. */
        phys_vec3_t mesh_origin = {0, 0, 0};
        for (uint32_t b = 0; b < args->body_count; b++) {
            if (args->colliders[b].type == PHYS_SHAPE_MESH &&
                args->colliders[b].shape_index == m) {
                mesh_origin = args->bodies_prev[b].position;
                break;
            }
        }

        /* Query BVH with the swept AABB (in mesh-local space). */
        phys_aabb_t local_aabb;
        local_aabb.min = vec3_sub(swept_aabb.min, mesh_origin);
        local_aabb.max = vec3_sub(swept_aabb.max, mesh_origin);

        uint32_t cands[CCD_MAX_CANDIDATES];
        uint32_t nc = ccd_collect_candidates(
            &ms->bvh, &local_aabb, cands, CCD_MAX_CANDIDATES);
        if (nc == 0) continue;

        /* Test each subsample position. */
        for (int si = 0; si < n_samples; si++) {
            /* Parametric t in [-margin_t, 1+margin_t] mapped to
             * subsample index.  margin_t extends the sweep slightly
             * before prev and after curr. */
            float margin_t = (disp_len > 1e-6f)
                ? (margin / disp_len) : 0.0f;
            float t = -margin_t + (1.0f + 2.0f * margin_t)
                    * ((float)si / (float)(n_samples - 1));

            /* Interpolate body position and orientation at time t.
             * Clamp the actual lerp parameter for position to avoid
             * extrapolating too far, but allow the full range for
             * the sweep test. */
            phys_vec3_t pos = vec3_add(prev->position,
                vec3_scale(displacement, t));
            phys_quat_t ori = ccd_quat_slerp(
                prev->orientation, curr->orientation,
                (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t));

            /* Compute capsule endpoints in world space. */
            phys_vec3_t offset = ccd_quat_rotate(ori, col->local_offset);
            phys_vec3_t center = vec3_add(pos, offset);
            phys_quat_t world_rot = ccd_quat_mul(ori, col->local_rotation);
            phys_vec3_t axis = ccd_quat_rotate(
                world_rot, (phys_vec3_t){0, half_h, 0});
            phys_vec3_t ep_a = vec3_add(center, axis);
            phys_vec3_t ep_b = vec3_sub(center, axis);

            /* Transform to mesh-local space. */
            phys_vec3_t local_a = vec3_sub(ep_a, mesh_origin);
            phys_vec3_t local_b = vec3_sub(ep_b, mesh_origin);

            /* Test capsule SDF against each candidate triangle. */
            for (uint32_t ci = 0; ci < nc; ci++) {
                if (cands[ci] >= ms->bvh.tri_count) continue;
                const phys_triangle_t *tri = &ms->triangles[cands[ci]];

                phys_vec3_t hit_normal;
                float pen = capsule_vs_triangle_depth(
                    local_a, local_b, radius, tri, &hit_normal);

                if (pen > 0.0f && t < best_t) {
                    best_t = t;
                    best_normal = hit_normal;
                    any_hit = true;
                    break; /* Earliest sample hit — stop inner loop. */
                }
            }
            if (any_hit && best_t <= t) break; /* Found earliest. */
        }
    }

    if (any_hit) {
        /* Clamp parametric t to [0, 1] for position clamping. */
        if (best_t < 0.0f) best_t = 0.0f;
        *best_t_out = best_t;
        *best_normal_out = best_normal;
    }
    return any_hit;
}

/* ── Swept sphere vs all meshes ────────────────────────────────── */

/**
 * Sweep a sphere from start to end against all mesh shapes.
 * The sweep is extended by @p margin in both directions along
 * the motion vector (fudge factor to catch boundary crossings).
 */
static bool ccd_sweep_sphere_vs_meshes(
    phys_vec3_t start, phys_vec3_t end_pos, float radius,
    float margin, const phys_ccd_args_t *args,
    float *best_t, phys_vec3_t *best_normal, phys_vec3_t *best_hit)
{
    /* Extend sweep by margin before start and after end. */
    phys_vec3_t motion = vec3_sub(end_pos, start);
    float motion_len = sqrtf(vec3_dot(motion, motion));
    phys_vec3_t ext_start = start;
    phys_vec3_t ext_end = end_pos;
    float margin_t = 0.0f;
    if (motion_len > 1e-6f) {
        phys_vec3_t dir = vec3_scale(motion, 1.0f / motion_len);
        ext_start = vec3_sub(start, vec3_scale(dir, margin));
        ext_end   = vec3_add(end_pos, vec3_scale(dir, margin));
        margin_t = margin / motion_len;
    }

    bool any_hit = false;

    for (uint32_t m = 0; m < args->mesh_count; m++) {
        const phys_mesh_shape_t *ms =
            &((const phys_mesh_shape_t *)args->meshes)[m];
        if (!ms->triangles || ms->bvh.node_count == 0) continue;

        phys_vec3_t mesh_origin = {0, 0, 0};
        for (uint32_t b = 0; b < args->body_count; b++) {
            if (args->colliders[b].type == PHYS_SHAPE_MESH &&
                args->colliders[b].shape_index == m) {
                mesh_origin = args->bodies_prev[b].position;
                break;
            }
        }

        phys_vec3_t local_start = vec3_sub(ext_start, mesh_origin);
        phys_vec3_t local_end   = vec3_sub(ext_end, mesh_origin);

        float t;
        phys_vec3_t normal, hit_pos;
        if (phys_swept_sphere_vs_mesh(local_start, local_end, radius,
                                       ms->triangles, &ms->bvh,
                                       &t, &normal, &hit_pos)) {
            /* Map t from extended interval back to original [0,1]. */
            float orig_t = (t * (1.0f + 2.0f * margin_t)) - margin_t;
            if (orig_t < 0.0f) orig_t = 0.0f;
            if (orig_t < *best_t) {
                *best_t = orig_t;
                *best_normal = normal;
                *best_hit = vec3_add(hit_pos, mesh_origin);
                any_hit = true;
            }
        }
    }
    return any_hit;
}

/* ── Capsule depenetration vs mesh ─────────────────────────────── */

/**
 * Check if a capsule (endpoints ep_a, ep_b, radius) is penetrating
 * any mesh.  Returns deepest penetration depth and push-out normal.
 */
static bool ccd_depenetrate_capsule_vs_meshes(
    phys_vec3_t ep_a, phys_vec3_t ep_b, float radius,
    const phys_ccd_args_t *args,
    float *depth_out, phys_vec3_t *normal_out)
{
    float deepest = 0.0f;
    phys_vec3_t best_normal = {0, 1, 0};
    bool any = false;

    for (uint32_t m = 0; m < args->mesh_count; m++) {
        const phys_mesh_shape_t *ms =
            &((const phys_mesh_shape_t *)args->meshes)[m];
        if (!ms->triangles || ms->bvh.node_count == 0) continue;

        phys_vec3_t mesh_origin = {0, 0, 0};
        for (uint32_t b = 0; b < args->body_count; b++) {
            if (args->colliders[b].type == PHYS_SHAPE_MESH &&
                args->colliders[b].shape_index == m) {
                mesh_origin = args->bodies_prev[b].position;
                break;
            }
        }

        /* Build AABB around the capsule in mesh-local space. */
        phys_vec3_t la = vec3_sub(ep_a, mesh_origin);
        phys_vec3_t lb = vec3_sub(ep_b, mesh_origin);
        phys_aabb_t query;
        query.min.x = fminf(la.x, lb.x) - radius;
        query.min.y = fminf(la.y, lb.y) - radius;
        query.min.z = fminf(la.z, lb.z) - radius;
        query.max.x = fmaxf(la.x, lb.x) + radius;
        query.max.y = fmaxf(la.y, lb.y) + radius;
        query.max.z = fmaxf(la.z, lb.z) + radius;

        uint32_t cands[CCD_MAX_CANDIDATES];
        uint32_t nc = ccd_collect_candidates(
            &ms->bvh, &query, cands, CCD_MAX_CANDIDATES);

        for (uint32_t i = 0; i < nc; i++) {
            if (cands[i] >= ms->bvh.tri_count) continue;
            const phys_triangle_t *tri = &ms->triangles[cands[i]];

            phys_vec3_t hit_normal;
            float pen = capsule_vs_triangle_depth(
                la, lb, radius, tri, &hit_normal);

            if (pen > deepest) {
                deepest = pen;
                best_normal = hit_normal;
                any = true;
            }
        }
    }

    if (any) {
        *depth_out = deepest;
        *normal_out = best_normal;
    }
    return any;
}


/** Slight radius inflation for CCD.  Adds safety margin at constraint
 *  junctions where capsule cross-sections are thinnest. */
#define CCD_INFLATE 0.02f

/**
 * Process a single body for CCD (depenetration + swept test).
 * Extracted so the main loop stays readable.
 */
static int ccd_process_body(
    uint32_t i, const phys_ccd_args_t *args,
    float radius, float half_h, float ccd_radius, float margin)
{
    phys_body_t *prev = &args->bodies_prev[i];
    phys_body_t *curr = &args->bodies_curr[i];
    const phys_collider_t *col = &args->colliders[i];

    /* ── Phase 1: Depenetration ──────────────────────────────
     * If the post-integration position is already penetrating
     * the mesh, push it out.  Catches solver/joint drift. */
    {
        float depth = 0.0f;
        phys_vec3_t normal = {0, 1, 0};
        bool penetrating = false;

        if (col->type == PHYS_SHAPE_CAPSULE && half_h > 0.0f) {
            phys_vec3_t offset = ccd_quat_rotate(
                curr->orientation, col->local_offset);
            phys_vec3_t center = vec3_add(curr->position, offset);
            phys_quat_t wr = ccd_quat_mul(
                curr->orientation, col->local_rotation);
            phys_vec3_t ax = ccd_quat_rotate(
                wr, (phys_vec3_t){0, half_h, 0});
            phys_vec3_t ep_a = vec3_add(center, ax);
            phys_vec3_t ep_b = vec3_sub(center, ax);

            penetrating = ccd_depenetrate_capsule_vs_meshes(
                ep_a, ep_b, ccd_radius, args, &depth, &normal);
        } else {
            phys_vec3_t center = vec3_add(
                curr->position, col->local_offset);
            penetrating = ccd_depenetrate_sphere_vs_meshes(
                center, ccd_radius, args, &depth, &normal);
        }

        if (penetrating) {
            curr->position = vec3_add(curr->position,
                vec3_scale(normal, depth + 0.01f));
            float vn = vec3_dot(curr->linear_vel, normal);
            if (vn < 0.0f) {
                curr->linear_vel = vec3_sub(
                    curr->linear_vel, vec3_scale(normal, vn));
            }
            return 1;
        }
    }

    /* ── Phase 2: Swept CCD ──────────────────────────────── */
    phys_vec3_t displacement = vec3_sub(curr->position, prev->position);
    float disp_len = sqrtf(vec3_dot(displacement, displacement));
    if (disp_len < radius * 0.5f) return 0;

    float best_t = 2.0f;
    phys_vec3_t best_normal = {0, 1, 0};

    if (col->type == PHYS_SHAPE_CAPSULE && half_h > 0.0f) {
        ccd_swept_capsule_vs_meshes(
            prev, curr, col, ccd_radius, half_h,
            args, margin, &best_t, &best_normal);
    } else {
        phys_vec3_t start = vec3_add(prev->position, col->local_offset);
        phys_vec3_t end_pos = vec3_add(curr->position, col->local_offset);
        phys_vec3_t best_hit = {0, 0, 0};
        ccd_sweep_sphere_vs_meshes(
            start, end_pos, ccd_radius, margin, args,
            &best_t, &best_normal, &best_hit);
    }

    if (best_t <= 1.01f) {
        if (best_t > 1.0f) best_t = 1.0f;
        phys_vec3_t safe_pos = vec3_add(prev->position,
            vec3_scale(displacement, best_t));
        safe_pos = vec3_add(safe_pos,
            vec3_scale(best_normal, 0.01f));
        curr->position = safe_pos;

        float vn = vec3_dot(curr->linear_vel, best_normal);
        if (vn < 0.0f) {
            curr->linear_vel = vec3_sub(
                curr->linear_vel, vec3_scale(best_normal, vn));
        }
        return 1;
    }
    return 0;
}

int phys_stage_ccd(const phys_ccd_args_t *args) {
    if (!args) return 0;
    if (!args->bodies_prev || !args->bodies_curr || !args->bodies_read)
        return 0;
    if (!args->colliders || args->mesh_count == 0) return 0;

    const uint32_t n = args->body_count;
    const uint32_t mask_words = (n + 31) / 32;

    /* Arena-allocated bitmask: which bodies to run CCD on.
     * Reclaimed automatically when the frame arena resets. */
    if (!args->arena) return 0;
    uint32_t *ccd_mask = phys_frame_arena_alloc(
        args->arena, mask_words * sizeof(uint32_t), 4);
    if (!ccd_mask) return 0;
    memset(ccd_mask, 0, mask_words * sizeof(uint32_t));

    /* Pass 1: mark bodies that are explicitly CCD-enabled and dynamic. */
    for (uint32_t i = 0; i < n; i++) {
        const phys_body_t *read = &args->bodies_read[i];
        if (!(read->flags & PHYS_BODY_FLAG_CCD)) continue;
        if (read->flags & (PHYS_BODY_FLAG_STATIC | PHYS_BODY_FLAG_KINEMATIC))
            continue;
        if (read->inv_mass <= 0.0f) continue;
        ccd_mask[i / 32] |= (1u << (i % 32));
    }

    /* Pass 2: propagate to immediate constraint neighbors.
     * If body A is marked, also mark body B (and vice versa),
     * but only if the neighbor is dynamic and has a supported shape. */
    if (args->constraints) {
        for (uint32_t ci = 0; ci < args->constraint_count; ci++) {
            const phys_constraint_t *c = &args->constraints[ci];
            uint32_t a = c->body_a, b = c->body_b;
            if (a >= n || b >= n) continue;

            bool a_marked = (ccd_mask[a / 32] >> (a % 32)) & 1u;
            bool b_marked = (ccd_mask[b / 32] >> (b % 32)) & 1u;
            if (!a_marked && !b_marked) continue;

            /* Propagate to the unmarked neighbor if it's dynamic. */
            if (a_marked && !b_marked) {
                const phys_body_t *rb = &args->bodies_read[b];
                if (rb->inv_mass > 0.0f &&
                    !(rb->flags & (PHYS_BODY_FLAG_STATIC |
                                   PHYS_BODY_FLAG_KINEMATIC))) {
                    ccd_mask[b / 32] |= (1u << (b % 32));
                }
            }
            if (b_marked && !a_marked) {
                const phys_body_t *ra = &args->bodies_read[a];
                if (ra->inv_mass > 0.0f &&
                    !(ra->flags & (PHYS_BODY_FLAG_STATIC |
                                   PHYS_BODY_FLAG_KINEMATIC))) {
                    ccd_mask[a / 32] |= (1u << (a % 32));
                }
            }
        }
    }

    /* Pass 3: run CCD on all marked bodies. */
    int clamped = 0;

    for (uint32_t i = 0; i < n; i++) {
        if (!((ccd_mask[i / 32] >> (i % 32)) & 1u)) continue;

        const phys_collider_t *col = &args->colliders[i];

        float radius = 0.0f;
        float half_h = 0.0f;
        if (col->type == PHYS_SHAPE_SPHERE) {
            const phys_sphere_t *s =
                &((const phys_sphere_t *)args->spheres)[col->shape_index];
            radius = s->radius;
        } else if (col->type == PHYS_SHAPE_CAPSULE) {
            const phys_capsule_t *cap =
                &((const phys_capsule_t *)args->capsules)[col->shape_index];
            radius = cap->radius;
            half_h = cap->half_height;
        } else {
            continue;
        }

        /* Slight inflate for safety at constraint junctions. */
        float ccd_radius = radius + CCD_INFLATE;
        float margin = ccd_radius;

        clamped += ccd_process_body(i, args, radius, half_h,
                                     ccd_radius, margin);
    }

    /* No free needed — arena memory reclaimed on frame reset. */
    return clamped;
}
