/**
 * @file narrowphase_mesh.c
 * @brief BVH-accelerated primitive-vs-mesh narrowphase.
 *
 * Traverses the mesh BVH to find candidate triangles whose AABBs
 * overlap the primitive's AABB, then dispatches to the per-triangle
 * narrowphase test.  Keeps up to max_contacts results.
 */

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/mesh_narrowphase.h"
#include "ferrum/physics/phys_quat.h"

/** Rotate vector by quaternion: q * v * q^-1. */
static phys_vec3_t quat_rotate_vec3(phys_quat_t q, phys_vec3_t v) {
    phys_vec3_t u = {q.x, q.y, q.z};
    float s = q.w;
    phys_vec3_t t = vec3_scale(vec3_cross(u, v), 2.0f);
    return vec3_add(v, vec3_add(vec3_scale(t, s), vec3_cross(u, t)));
}

/* ── AABB builders for primitives ──────────────────────────────── */

static phys_aabb_t sphere_aabb(phys_vec3_t center, float radius,
                                float margin) {
    float r = radius + margin;
    phys_aabb_t a;
    a.min = (phys_vec3_t){center.x - r, center.y - r, center.z - r};
    a.max = (phys_vec3_t){center.x + r, center.y + r, center.z + r};
    return a;
}

static phys_aabb_t box_aabb(phys_vec3_t center, phys_quat_t rot,
                             phys_vec3_t he, float margin) {
    phys_vec3_t ax[3];
    ax[0] = quat_rotate_vec3(rot, (phys_vec3_t){1, 0, 0});
    ax[1] = quat_rotate_vec3(rot, (phys_vec3_t){0, 1, 0});
    ax[2] = quat_rotate_vec3(rot, (phys_vec3_t){0, 0, 1});

    float rx = fabsf(ax[0].x)*he.x + fabsf(ax[1].x)*he.y + fabsf(ax[2].x)*he.z + margin;
    float ry = fabsf(ax[0].y)*he.x + fabsf(ax[1].y)*he.y + fabsf(ax[2].y)*he.z + margin;
    float rz = fabsf(ax[0].z)*he.x + fabsf(ax[1].z)*he.y + fabsf(ax[2].z)*he.z + margin;

    phys_aabb_t a;
    a.min = (phys_vec3_t){center.x - rx, center.y - ry, center.z - rz};
    a.max = (phys_vec3_t){center.x + rx, center.y + ry, center.z + rz};
    return a;
}

static phys_aabb_t capsule_aabb(phys_vec3_t center, phys_quat_t rot,
                                 float radius, float half_height,
                                 float margin) {
    phys_vec3_t up = quat_rotate_vec3(rot, (phys_vec3_t){0, 1, 0});
    phys_vec3_t a = vec3_sub(center, vec3_scale(up, half_height));
    phys_vec3_t b = vec3_add(center, vec3_scale(up, half_height));
    float r = radius + margin;

    phys_aabb_t aabb;
    aabb.min.x = (a.x < b.x ? a.x : b.x) - r;
    aabb.min.y = (a.y < b.y ? a.y : b.y) - r;
    aabb.min.z = (a.z < b.z ? a.z : b.z) - r;
    aabb.max.x = (a.x > b.x ? a.x : b.x) + r;
    aabb.max.y = (a.y > b.y ? a.y : b.y) + r;
    aabb.max.z = (a.z > b.z ? a.z : b.z) + r;
    return aabb;
}

/* ── BVH traversal (shared) ────────────────────────────────────── */

#define MESH_NP_STACK_CAP 128u
#define MESH_NP_MAX_CANDIDATES 64u

/**
 * @brief Traverse mesh BVH, collect triangle indices overlapping query AABB.
 * @return Number of candidate triangle indices written.
 */
static uint32_t collect_candidates(const phys_mesh_bvh_t *bvh,
                                    const phys_aabb_t *query,
                                    uint32_t *out_tris,
                                    uint32_t max_tris) {
    if (!bvh || !bvh->nodes || bvh->node_count == 0) return 0;
    if (bvh->root >= bvh->node_count) return 0;

    uint32_t stack[MESH_NP_STACK_CAP];
    uint32_t sp = 0;
    stack[sp++] = bvh->root;

    uint32_t count = 0;
    while (sp && count < max_tris) {
        uint32_t idx = stack[--sp];
        if (idx >= bvh->node_count) continue;
        const phys_mesh_bvh_node_t *n = &bvh->nodes[idx];
        if (!phys_aabb_overlap(&n->bounds, query)) continue;

        if (phys_mesh_bvh_node_is_leaf(n)) {
            out_tris[count++] = n->tri_index;
        } else {
            if (sp + 2 <= MESH_NP_STACK_CAP) {
                stack[sp++] = n->left;
                stack[sp++] = n->right;
            }
        }
    }
    return count;
}

/* ── Sphere vs Mesh ────────────────────────────────────────────── */

int phys_sphere_vs_mesh(
    phys_vec3_t center, float radius,
    const phys_triangle_t *triangles,
    const phys_mesh_bvh_t *bvh,
    float spec_margin,
    bool solid,
    phys_contact_point_t *contacts_out,
    int max_contacts)
{
    if (!triangles || !bvh || !contacts_out || max_contacts <= 0) return 0;

    phys_aabb_t query = sphere_aabb(center, radius, spec_margin);
    uint32_t cands[MESH_NP_MAX_CANDIDATES];
    uint32_t nc = collect_candidates(bvh, &query, cands, MESH_NP_MAX_CANDIDATES);

    int out = 0;
    for (uint32_t i = 0; i < nc && out < max_contacts; i++) {
        if (cands[i] >= bvh->tri_count) continue;
        phys_contact_point_t c;
        memset(&c, 0, sizeof(c));
        if (phys_sphere_vs_triangle(center, radius,
                                     &triangles[cands[i]],
                                     spec_margin, solid, &c)) {
            contacts_out[out++] = c;
        }
    }
    return out;
}

/* ── Box vs Mesh ───────────────────────────────────────────────── */

int phys_box_vs_mesh(
    phys_vec3_t box_center, phys_quat_t box_rotation,
    phys_vec3_t box_half_extents,
    const phys_triangle_t *triangles,
    const phys_mesh_bvh_t *bvh,
    float spec_margin,
    phys_contact_point_t *contacts_out,
    int max_contacts)
{
    if (!triangles || !bvh || !contacts_out || max_contacts <= 0) return 0;

    phys_aabb_t query = box_aabb(box_center, box_rotation,
                                  box_half_extents, spec_margin);
    uint32_t cands[MESH_NP_MAX_CANDIDATES];
    uint32_t nc = collect_candidates(bvh, &query, cands, MESH_NP_MAX_CANDIDATES);

    int out = 0;
    for (uint32_t i = 0; i < nc && out < max_contacts; i++) {
        if (cands[i] >= bvh->tri_count) continue;
        phys_contact_point_t c;
        memset(&c, 0, sizeof(c));
        int n = phys_box_vs_triangle(box_center, box_rotation,
                                      box_half_extents,
                                      &triangles[cands[i]],
                                      spec_margin, &c, 1);
        if (n > 0) {
            contacts_out[out++] = c;
        }
    }
    return out;
}

/* ── Capsule vs Mesh ───────────────────────────────────────────── */

int phys_capsule_vs_mesh(
    phys_vec3_t cap_center, phys_quat_t cap_rotation,
    float cap_radius, float cap_half_height,
    const phys_triangle_t *triangles,
    const phys_mesh_bvh_t *bvh,
    float spec_margin,
    bool solid,
    phys_contact_point_t *contacts_out,
    int max_contacts)
{
    if (!triangles || !bvh || !contacts_out || max_contacts <= 0) return 0;

    phys_aabb_t query = capsule_aabb(cap_center, cap_rotation,
                                      cap_radius, cap_half_height,
                                      spec_margin);
    uint32_t cands[MESH_NP_MAX_CANDIDATES];
    uint32_t nc = collect_candidates(bvh, &query, cands, MESH_NP_MAX_CANDIDATES);

    int out = 0;
    for (uint32_t i = 0; i < nc && out < max_contacts; i++) {
        if (cands[i] >= bvh->tri_count) continue;
        phys_contact_point_t c;
        memset(&c, 0, sizeof(c));
        if (phys_capsule_vs_triangle(cap_center, cap_rotation,
                                      cap_radius, cap_half_height,
                                      &triangles[cands[i]],
                                      spec_margin, solid, &c)) {
            contacts_out[out++] = c;
        }
    }
    return out;
}
