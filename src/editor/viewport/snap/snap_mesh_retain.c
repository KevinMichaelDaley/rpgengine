/**
 * @file snap_mesh_retain.c
 * @brief Retain CPU-side mesh data for snap raycasting.
 *
 * Called during mesh loading (before mesh_slot is destroyed) to copy
 * positions, normals, and indices into the snap mesh cache.
 *
 * Non-static functions (2 / 4 limit):
 *   snap_mesh_retain_from_slot
 *   snap_mesh_retain_box
 */

#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/editor/mesh/mesh_slot.h"

void snap_mesh_retain_from_slot(snap_mesh_cache_t *cache,
                                 uint32_t entity_id,
                                 const mesh_slot_t *slot) {
    if (!cache || !slot) return;
    if (!slot->positions || !slot->normals || !slot->indices) return;
    if (slot->vertex_count == 0 || slot->index_count == 0) return;

    snap_mesh_cache_insert(cache, entity_id,
                            slot->positions, slot->normals, slot->indices,
                            slot->vertex_count, slot->index_count);
}

void snap_mesh_retain_box(snap_mesh_cache_t *cache,
                           uint32_t entity_id) {
    if (!cache) return;

    /* Unit box: 8 vertices, 12 triangles (36 indices).
     * Centered at origin, half-extent = 0.5 on each axis. */
    static const float positions[] = {
        /* Front face (+Z). */
        -0.5f, -0.5f,  0.5f,   0.5f, -0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,  -0.5f,  0.5f,  0.5f,
        /* Back face (-Z). */
        -0.5f, -0.5f, -0.5f,  -0.5f,  0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,   0.5f, -0.5f, -0.5f,
        /* Top face (+Y). */
        -0.5f,  0.5f, -0.5f,  -0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,   0.5f,  0.5f, -0.5f,
        /* Bottom face (-Y). */
        -0.5f, -0.5f, -0.5f,   0.5f, -0.5f, -0.5f,
         0.5f, -0.5f,  0.5f,  -0.5f, -0.5f,  0.5f,
        /* Right face (+X). */
         0.5f, -0.5f, -0.5f,   0.5f,  0.5f, -0.5f,
         0.5f,  0.5f,  0.5f,   0.5f, -0.5f,  0.5f,
        /* Left face (-X). */
        -0.5f, -0.5f, -0.5f,  -0.5f, -0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f,  -0.5f,  0.5f, -0.5f,
    };
    static const float normals[] = {
        /* Front (+Z). */
        0, 0, 1,   0, 0, 1,   0, 0, 1,   0, 0, 1,
        /* Back (-Z). */
        0, 0, -1,  0, 0, -1,  0, 0, -1,  0, 0, -1,
        /* Top (+Y). */
        0, 1, 0,   0, 1, 0,   0, 1, 0,   0, 1, 0,
        /* Bottom (-Y). */
        0, -1, 0,  0, -1, 0,  0, -1, 0,  0, -1, 0,
        /* Right (+X). */
        1, 0, 0,   1, 0, 0,   1, 0, 0,   1, 0, 0,
        /* Left (-X). */
        -1, 0, 0,  -1, 0, 0,  -1, 0, 0,  -1, 0, 0,
    };
    static const uint32_t indices[] = {
        0,  1,  2,   0,  2,  3,   /* Front. */
        4,  5,  6,   4,  6,  7,   /* Back. */
        8,  9,  10,  8,  10, 11,  /* Top. */
        12, 13, 14,  12, 14, 15,  /* Bottom. */
        16, 17, 18,  16, 18, 19,  /* Right. */
        20, 21, 22,  20, 22, 23,  /* Left. */
    };

    snap_mesh_cache_insert(cache, entity_id,
                            positions, normals, indices, 24, 36);
}

