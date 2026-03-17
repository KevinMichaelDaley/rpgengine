/**
 * @file narrowphase_mesh_mesh.c
 * @brief BVH-accelerated mesh-vs-mesh narrowphase.
 *
 * Performs dual BVH traversal: queries mesh B's BVH with each
 * triangle A's AABB to find candidate pairs, then tests each
 * pair with phys_triangle_vs_triangle.
 *
 * Non-static functions (1 / 4 limit):
 *   phys_mesh_vs_mesh
 */

#include "ferrum/physics/mesh_narrowphase.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/aabb.h"

#include <string.h>

/* ---- BVH query (shared with narrowphase_mesh.c) ---- */

#define MESH_NP_STACK_CAP 128u
#define MESH_NP_MAX_CANDIDATES 64u

/**
 * @brief Traverse mesh BVH, collect triangle indices overlapping query AABB.
 */
static uint32_t collect_candidates_(const phys_mesh_bvh_t *bvh,
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

/* ---- Public API ---- */

int phys_mesh_vs_mesh(
    const phys_triangle_t *tris_a,
    const phys_mesh_bvh_t *bvh_a,
    const phys_triangle_t *tris_b,
    const phys_mesh_bvh_t *bvh_b,
    float spec_margin,
    phys_contact_point_t *contacts_out,
    int max_contacts)
{
    if (!tris_a || !bvh_a || !tris_b || !bvh_b) return 0;
    if (!contacts_out || max_contacts <= 0) return 0;

    int out = 0;

    /* For each triangle in mesh A, query mesh B's BVH for overlaps. */
    for (uint32_t i = 0; i < bvh_a->tri_count && out < max_contacts; ++i) {
        /* Build AABB for triangle A. */
        phys_aabb_t a_aabb = phys_triangle_aabb(&tris_a[i]);
        /* Expand by speculative margin. */
        a_aabb.min.x -= spec_margin;
        a_aabb.min.y -= spec_margin;
        a_aabb.min.z -= spec_margin;
        a_aabb.max.x += spec_margin;
        a_aabb.max.y += spec_margin;
        a_aabb.max.z += spec_margin;

        /* Query B's BVH. */
        uint32_t cands[MESH_NP_MAX_CANDIDATES];
        uint32_t nc = collect_candidates_(bvh_b, &a_aabb, cands,
                                            MESH_NP_MAX_CANDIDATES);

        for (uint32_t j = 0; j < nc && out < max_contacts; ++j) {
            if (cands[j] >= bvh_b->tri_count) continue;

            phys_contact_point_t c;
            memset(&c, 0, sizeof(c));
            if (phys_triangle_vs_triangle(&tris_a[i], &tris_b[cands[j]],
                                            spec_margin, &c)) {
                contacts_out[out++] = c;
            }
        }
    }

    return out;
}
