/**
 * @file snap_mesh_retain_decomposed.c
 * @brief Convex decomposition path for snap mesh retention.
 *
 * For high-poly meshes, runs V-ACD convex decomposition and inserts
 * the simplified convex hull compound into the snap cache. This
 * reduces a 50K+ triangle mesh to a few hundred triangles, preventing
 * O(n) raycast and O(n*m) depenetration from crashing the editor.
 *
 * Non-static functions (2 / 4 limit):
 *   snap_mesh_should_decompose
 *   snap_mesh_retain_decomposed
 */

#include "ferrum/editor/viewport/snap/snap_mesh_decompose.h"
#include "ferrum/editor/viewport/snap/snap_decompose_cache.h"
#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/physics/convex_decompose.h"
#include "ferrum/physics/convex_compound.h"
#include "ferrum/physics/mesh_collider.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Validate and sanitize a convex hull's face indices.
 *
 * The V-ACD convex hull builder can produce degenerate faces with
 * sentinel index values (0xFFFF). This function removes faces that
 * reference out-of-bounds vertex indices, making the hull safe for
 * fan triangulation.
 */
static void sanitize_hull_(phys_convex_hull_t *hull) {
    uint32_t valid_face_count = 0;
    for (uint32_t f = 0; f < hull->face_count; f++) {
        const phys_convex_face_t *face = &hull->faces[f];
        if (face->index_count < 3) continue;

        /* Check all indices in this face. */
        bool face_valid = true;
        for (uint32_t i = 0; i < face->index_count; i++) {
            uint32_t pos = face->index_start + i;
            if (pos >= hull->index_count ||
                hull->indices[pos] >= hull->vertex_count) {
                face_valid = false;
                break;
            }
        }

        if (face_valid) {
            if (valid_face_count != f) {
                hull->faces[valid_face_count] = hull->faces[f];
            }
            valid_face_count++;
        }
    }
    hull->face_count = valid_face_count;
}

bool snap_mesh_should_decompose(const mesh_slot_t *slot) {
    if (!slot) return false;
    uint32_t tri_count = slot->index_count / 3;
    return tri_count > SNAP_DECOMPOSE_THRESHOLD;
}

bool snap_mesh_retain_decomposed(snap_mesh_cache_t *cache,
                                  uint32_t entity_id,
                                  const mesh_slot_t *slot,
                                  snap_decompose_cache_t *dcache) {
    if (!cache || !slot) return false;
    if (!slot->positions || !slot->indices) return false;
    if (slot->vertex_count == 0 || slot->index_count < 3) return false;

    uint32_t tri_count = slot->index_count / 3;

    /* Build phys_triangle_t array from mesh slot data. */
    phys_triangle_t *triangles = malloc(tri_count * sizeof(phys_triangle_t));
    if (!triangles) return false;

    for (uint32_t t = 0; t < tri_count; t++) {
        for (int v = 0; v < 3; v++) {
            uint32_t vi = slot->indices[t * 3 + v];
            triangles[t].v[v].x = slot->positions[vi * 3 + 0];
            triangles[t].v[v].y = slot->positions[vi * 3 + 1];
            triangles[t].v[v].z = slot->positions[vi * 3 + 2];
        }
    }

    /* Run V-ACD convex decomposition.
     * Result is heap-allocated because phys_decompose_result_t is ~167KB
     * (64 inline convex hulls) — too large for stack, especially on fibers. */
    phys_decompose_params_t params = phys_decompose_params_default();
    phys_decompose_result_t *result = malloc(sizeof(phys_decompose_result_t));
    if (!result) { free(triangles); return false; }
    memset(result, 0, sizeof(*result));

    int rc = phys_decompose_mesh(triangles, tri_count, &params, result);
    free(triangles);

    if (rc != 0 || result->hull_count == 0) {
        free(result);
        return false;
    }

    /* Sanitize hulls — the V-ACD builder can produce degenerate faces
     * with sentinel vertex indices (0xFFFF). */
    for (uint32_t i = 0; i < result->hull_count; i++) {
        sanitize_hull_(&result->hulls[i]);
    }

    /* Build a temporary compound referencing the result hulls directly.
     * snap_mesh_retain_compound expects hulls[] as a flat array and
     * compound->child_hull_indices as indices into it. Since our result
     * hulls ARE the flat array, indices are simply 0..hull_count-1. */
    phys_convex_compound_t compound;
    compound.child_count = result->hull_count;
    for (uint32_t i = 0; i < result->hull_count; i++) {
        compound.child_hull_indices[i] = i;
    }

    snap_mesh_retain_compound(cache, entity_id, result->hulls, &compound);

    /* Store the decomposition result in the cache if provided.
     * This allows reuse for physics body creation without re-running V-ACD. */
    if (dcache) {
        snap_decompose_cache_set(dcache, entity_id, result);
    }

    free(result);
    return true;
}
