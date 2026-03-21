/**
 * @file mesh_bone_collision_apply.c
 * @brief Apply per-bone collision data to skeleton definitions.
 *
 * Converts mesh_bone_collision_set_t entries into bone_collider_desc_t
 * arrays and hull vertex buffers, then applies them to a skeleton
 * definition copy for per-instance collision override.
 *
 * Non-static functions (2 / 4 limit):
 *   mesh_bone_collision_to_collider_descs
 *   mesh_bone_collision_override_skeleton
 */

#include "ferrum/editor/mesh/mesh_bone_collision.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/bone_collider.h"
#include "ferrum/physics/convex_hull.h"

#include <stdlib.h>
#include <string.h>

bool mesh_bone_collision_to_collider_descs(
    const mesh_bone_collision_set_t *set,
    bone_collider_desc_t *out_descs,
    float *out_hull_verts,
    uint32_t *out_vert_count,
    uint32_t max_verts) {
    if (!set || !out_descs || !out_hull_verts || !out_vert_count) return false;

    uint32_t vert_offset = 0;

    for (uint32_t i = 0; i < set->count; i++) {
        const mesh_bone_collision_t *entry = &set->entries[i];
        bone_collider_desc_t *desc = &out_descs[i];
        memset(desc, 0, sizeof(*desc));

        if (!entry->valid || entry->decomp.hull_count == 0) {
            desc->shape_type = BONE_COLLIDER_NONE;
            continue;
        }

        /* Collect all hull vertices into the flat buffer. */
        uint32_t total_verts = 0;
        for (uint32_t h = 0; h < entry->decomp.hull_count; h++) {
            total_verts += entry->decomp.hulls[h].vertex_count;
        }

        if (vert_offset + total_verts > max_verts) {
            *out_vert_count = vert_offset;
            return false; /* Overflow. */
        }

        desc->shape_type = BONE_COLLIDER_CONVEX_HULL;
        desc->hull_offset = vert_offset;
        desc->hull_count = total_verts;

        /* Copy hull vertices (x,y,z triples). */
        for (uint32_t h = 0; h < entry->decomp.hull_count; h++) {
            const phys_convex_hull_t *hull = &entry->decomp.hulls[h];
            for (uint32_t v = 0; v < hull->vertex_count; v++) {
                out_hull_verts[(vert_offset + v) * 3 + 0] = hull->vertices[v].x;
                out_hull_verts[(vert_offset + v) * 3 + 1] = hull->vertices[v].y;
                out_hull_verts[(vert_offset + v) * 3 + 2] = hull->vertices[v].z;
            }
            vert_offset += hull->vertex_count;
        }
    }

    *out_vert_count = vert_offset;
    return true;
}

bool mesh_bone_collision_override_skeleton(
    skeleton_def_t *skel_copy,
    const mesh_bone_collision_set_t *set) {
    if (!skel_copy || !set) return false;
    if (skel_copy->joint_count == 0) return false;

    /* Count total hull vertices needed. */
    uint32_t total_hull_verts = 0;
    for (uint32_t i = 0; i < set->count; i++) {
        const mesh_bone_collision_t *entry = &set->entries[i];
        if (!entry->valid || entry->bone_index >= skel_copy->joint_count) {
            continue;
        }
        for (uint32_t h = 0; h < entry->decomp.hull_count; h++) {
            total_hull_verts += entry->decomp.hulls[h].vertex_count;
        }
    }

    /* Allocate colliders array if not already present. */
    if (!skel_copy->colliders) {
        skel_copy->colliders = (bone_collider_desc_t *)calloc(
            skel_copy->joint_count, sizeof(bone_collider_desc_t));
        if (!skel_copy->colliders) return false;
    }

    /* Free old hull vertices and allocate new. */
    free(skel_copy->hull_vertices);
    skel_copy->hull_vertices = NULL;
    skel_copy->hull_vertex_count = 0;

    if (total_hull_verts > 0) {
        skel_copy->hull_vertices = (float *)malloc(
            (size_t)total_hull_verts * 3 * sizeof(float));
        if (!skel_copy->hull_vertices) return false;
    }

    /* Fill collider descriptors and hull vertex buffer. */
    uint32_t vert_offset = 0;
    for (uint32_t i = 0; i < set->count; i++) {
        const mesh_bone_collision_t *entry = &set->entries[i];
        if (!entry->valid || entry->bone_index >= skel_copy->joint_count) {
            continue;
        }

        bone_collider_desc_t *desc = &skel_copy->colliders[entry->bone_index];
        desc->shape_type = BONE_COLLIDER_CONVEX_HULL;
        desc->hull_offset = vert_offset;

        uint32_t bone_verts = 0;
        for (uint32_t h = 0; h < entry->decomp.hull_count; h++) {
            const phys_convex_hull_t *hull = &entry->decomp.hulls[h];
            for (uint32_t v = 0; v < hull->vertex_count; v++) {
                skel_copy->hull_vertices[(vert_offset + v) * 3 + 0] =
                    hull->vertices[v].x;
                skel_copy->hull_vertices[(vert_offset + v) * 3 + 1] =
                    hull->vertices[v].y;
                skel_copy->hull_vertices[(vert_offset + v) * 3 + 2] =
                    hull->vertices[v].z;
            }
            vert_offset += hull->vertex_count;
            bone_verts += hull->vertex_count;
        }
        desc->hull_count = bone_verts;
    }

    skel_copy->hull_vertex_count = vert_offset;
    return true;
}
