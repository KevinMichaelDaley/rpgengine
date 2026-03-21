/**
 * @file mesh_bone_segment.c
 * @brief Per-bone triangle segmentation for skeletal meshes.
 *
 * Assigns each mesh triangle to its dominant bone based on
 * summed vertex weights, then builds per-bone phys_triangle_t
 * arrays for convex decomposition.
 *
 * Non-static functions (3 / 4 limit):
 *   mesh_bone_segments_init
 *   mesh_bone_segments_destroy
 *   mesh_bone_segments_from_slot
 */

#include "ferrum/editor/mesh/mesh_bone_segment.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/physics/mesh_collider.h"  /* phys_triangle_t */

#include <stdlib.h>
#include <string.h>

void mesh_bone_segments_init(mesh_bone_segments_t *segs, uint32_t capacity) {
    if (!segs) return;
    segs->capacity = capacity;
    segs->count = 0;
    if (capacity > 0) {
        segs->segments = (mesh_bone_segment_t *)calloc(
            capacity, sizeof(mesh_bone_segment_t));
    } else {
        segs->segments = NULL;
    }
}

void mesh_bone_segments_destroy(mesh_bone_segments_t *segs) {
    if (!segs || !segs->segments) return;
    for (uint32_t i = 0; i < segs->count; i++) {
        free(segs->segments[i].triangles);
    }
    free(segs->segments);
    segs->segments = NULL;
    segs->count = 0;
    segs->capacity = 0;
}

/**
 * @brief Determine the dominant bone for a triangle.
 *
 * Sums the bone weights for each bone across all three vertices
 * and returns the bone index with the highest total.
 */
static uint32_t dominant_bone_(const uint32_t *bone_indices,
                                const float *bone_weights,
                                uint32_t bones_per_vert,
                                uint32_t bone_count,
                                uint32_t vi0, uint32_t vi1, uint32_t vi2) {
    /* Accumulate weight per bone across all 3 vertices.
     * Use a temporary array capped at bone_count (heap for safety). */
    float *accum = (float *)calloc(bone_count, sizeof(float));
    if (!accum) return 0;

    const uint32_t verts[3] = {vi0, vi1, vi2};
    for (int v = 0; v < 3; v++) {
        uint32_t base = verts[v] * bones_per_vert;
        for (uint32_t b = 0; b < bones_per_vert; b++) {
            uint32_t bi = bone_indices[base + b];
            float w = bone_weights[base + b];
            if (bi < bone_count && w > 0.0f) {
                accum[bi] += w;
            }
        }
    }

    /* Find max. */
    uint32_t best = 0;
    float best_w = accum[0];
    for (uint32_t i = 1; i < bone_count; i++) {
        if (accum[i] > best_w) {
            best_w = accum[i];
            best = i;
        }
    }

    free(accum);
    return best;
}

bool mesh_bone_segments_from_slot(mesh_bone_segments_t *segs,
                                   const struct mesh_slot *slot,
                                   const uint32_t *bone_indices,
                                   const float *bone_weights,
                                   uint32_t bones_per_vert,
                                   uint32_t bone_count) {
    if (!segs || !slot || !bone_indices || !bone_weights) return false;
    if (!slot->positions || !slot->indices) return false;
    if (slot->index_count < 3 || bone_count == 0) return false;

    uint32_t tri_count = slot->index_count / 3;

    /* Phase 1: count triangles per bone. */
    uint32_t *counts = (uint32_t *)calloc(bone_count, sizeof(uint32_t));
    uint32_t *assignments = (uint32_t *)malloc(tri_count * sizeof(uint32_t));
    if (!counts || !assignments) {
        free(counts);
        free(assignments);
        return false;
    }

    for (uint32_t t = 0; t < tri_count; t++) {
        uint32_t i0 = slot->indices[t * 3 + 0];
        uint32_t i1 = slot->indices[t * 3 + 1];
        uint32_t i2 = slot->indices[t * 3 + 2];
        uint32_t bone = dominant_bone_(bone_indices, bone_weights,
                                        bones_per_vert, bone_count,
                                        i0, i1, i2);
        assignments[t] = bone;
        counts[bone]++;
    }

    /* Phase 2: allocate per-bone triangle arrays. */
    segs->count = 0;
    for (uint32_t b = 0; b < bone_count && segs->count < segs->capacity; b++) {
        if (counts[b] == 0) continue;

        mesh_bone_segment_t *seg = &segs->segments[segs->count];
        seg->bone_index = b;
        seg->tri_count = counts[b];
        seg->triangles = (phys_triangle_t *)malloc(
            counts[b] * sizeof(phys_triangle_t));
        if (!seg->triangles) {
            seg->tri_count = 0;
            continue;
        }
        segs->count++;
    }

    /* Phase 3: fill triangle arrays. */
    /* Reset counts to use as write cursors. */
    memset(counts, 0, bone_count * sizeof(uint32_t));

    for (uint32_t t = 0; t < tri_count; t++) {
        uint32_t bone = assignments[t];

        /* Find the segment for this bone. */
        mesh_bone_segment_t *seg = NULL;
        for (uint32_t s = 0; s < segs->count; s++) {
            if (segs->segments[s].bone_index == bone) {
                seg = &segs->segments[s];
                break;
            }
        }
        if (!seg || !seg->triangles) continue;

        uint32_t idx = counts[bone]++;
        if (idx >= seg->tri_count) continue;

        for (int v = 0; v < 3; v++) {
            uint32_t vi = slot->indices[t * 3 + v];
            seg->triangles[idx].v[v].x = slot->positions[vi * 3 + 0];
            seg->triangles[idx].v[v].y = slot->positions[vi * 3 + 1];
            seg->triangles[idx].v[v].z = slot->positions[vi * 3 + 2];
        }
    }

    free(counts);
    free(assignments);
    return true;
}
