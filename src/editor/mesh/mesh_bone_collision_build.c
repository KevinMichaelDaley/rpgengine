/**
 * @file mesh_bone_collision_build.c
 * @brief Build per-bone collision data from mesh segments.
 *
 * For each bone segment, runs V-ACD convex decomposition or builds
 * a single convex hull (for bones with fewer than 4 triangles).
 *
 * Non-static functions (2 / 4 limit):
 *   mesh_bone_collision_build
 *   mesh_bone_collision_destroy
 */

#include "ferrum/editor/mesh/mesh_bone_collision.h"
#include "ferrum/editor/mesh/mesh_bone_segment.h"
#include "ferrum/physics/convex_decompose.h"
#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/mesh_collider.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Build a single convex hull from a small triangle set.
 *
 * For bone segments with fewer than 4 triangles, we build a single
 * convex hull from the vertex point cloud rather than running V-ACD
 * which requires a minimum mesh size.
 */
static bool build_single_hull_(const phys_triangle_t *triangles,
                                uint32_t tri_count,
                                phys_decompose_result_t *result) {
    /* Collect unique vertices (up to 3 * tri_count). */
    uint32_t max_pts = tri_count * 3;
    phys_vec3_t *points = (phys_vec3_t *)malloc(max_pts * sizeof(phys_vec3_t));
    if (!points) return false;

    uint32_t pt_count = 0;
    for (uint32_t t = 0; t < tri_count; t++) {
        for (int v = 0; v < 3; v++) {
            points[pt_count++] = triangles[t].v[v];
        }
    }

    memset(result, 0, sizeof(*result));
    int rc = phys_convex_hull_build(&result->hulls[0], points, pt_count);
    free(points);

    if (rc != 0) return false;
    result->hull_count = 1;
    return true;
}

bool mesh_bone_collision_build(mesh_bone_collision_set_t *set,
                                const mesh_bone_segments_t *segments) {
    if (!set || !segments) return false;
    if (segments->count == 0) {
        set->entries = NULL;
        set->count = 0;
        return true;
    }

    set->entries = (mesh_bone_collision_t *)calloc(
        segments->count, sizeof(mesh_bone_collision_t));
    if (!set->entries) return false;
    set->count = segments->count;

    for (uint32_t i = 0; i < segments->count; i++) {
        const mesh_bone_segment_t *seg = &segments->segments[i];
        mesh_bone_collision_t *entry = &set->entries[i];
        entry->bone_index = seg->bone_index;
        entry->valid = false;

        if (!seg->triangles || seg->tri_count == 0) continue;

        if (seg->tri_count < 4) {
            /* Too few triangles for V-ACD; build single hull. */
            entry->valid = build_single_hull_(
                seg->triangles, seg->tri_count, &entry->decomp);
        } else {
            /* Run V-ACD convex decomposition. */
            phys_decompose_params_t params = phys_decompose_params_default();
            int rc = phys_decompose_mesh(
                seg->triangles, seg->tri_count, &params, &entry->decomp);
            entry->valid = (rc == 0 && entry->decomp.hull_count > 0);
        }
    }

    return true;
}

void mesh_bone_collision_destroy(mesh_bone_collision_set_t *set) {
    if (!set) return;
    free(set->entries);
    set->entries = NULL;
    set->count = 0;
}
