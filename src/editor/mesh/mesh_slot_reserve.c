/**
 * @file mesh_slot_reserve.c
 * @brief mesh_slot_t capacity management — reserve_vertices, reserve_indices.
 */
#include "ferrum/editor/mesh/mesh_slot.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------ */

/**
 * @brief Grow a float buffer from old_cap to new_cap elements.
 *        Copies existing data, zero-fills the new portion.
 */
static float *grow_float_buf_(float *old, uint32_t old_count,
                              uint32_t new_cap) {
    float *buf = realloc(old, (size_t)new_cap * sizeof(float));
    if (!buf) { return NULL; }
    /* Zero-fill the new region */
    if (new_cap > old_count) {
        memset(buf + old_count, 0,
               (size_t)(new_cap - old_count) * sizeof(float));
    }
    return buf;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

bool mesh_slot_reserve_vertices(mesh_slot_t *slot, uint32_t count) {
    if (!slot) { return false; }
    if (count > MESH_SLOT_MAX_VERTICES) { return false; }
    if (count <= slot->vertex_capacity) { return true; } /* already enough */

    /* Double until >= count */
    uint32_t new_cap = slot->vertex_capacity ? slot->vertex_capacity : 4;
    while (new_cap < count) { new_cap *= 2; }
    if (new_cap > MESH_SLOT_MAX_VERTICES) { new_cap = MESH_SLOT_MAX_VERTICES; }

    uint32_t old_v3 = slot->vertex_capacity * 3;
    uint32_t new_v3 = new_cap * 3;
    uint32_t old_v4 = slot->vertex_capacity * 4;
    uint32_t new_v4 = new_cap * 4;
    uint32_t old_v2 = slot->vertex_capacity * 2;
    uint32_t new_v2 = new_cap * 2;

    float *p = grow_float_buf_(slot->positions, old_v3, new_v3);
    float *n = grow_float_buf_(slot->normals,   old_v3, new_v3);
    float *t = grow_float_buf_(slot->tangents,  old_v4, new_v4);
    float *u0 = grow_float_buf_(slot->uvs[0],   old_v2, new_v2);
    float *u1 = grow_float_buf_(slot->uvs[1],   old_v2, new_v2);
    float *c = grow_float_buf_(slot->colors,    old_v4, new_v4);

    if (!p || !n || !t || !u0 || !u1 || !c) {
        /* On partial failure, keep whatever succeeded to avoid leaks.
         * The slot remains in a usable state with the old capacity. */
        if (p)  { slot->positions = p; }
        if (n)  { slot->normals   = n; }
        if (t)  { slot->tangents  = t; }
        if (u0) { slot->uvs[0]    = u0; }
        if (u1) { slot->uvs[1]    = u1; }
        if (c)  { slot->colors    = c; }
        return false;
    }

    slot->positions = p;
    slot->normals   = n;
    slot->tangents  = t;
    slot->uvs[0]    = u0;
    slot->uvs[1]    = u1;
    slot->colors    = c;
    slot->vertex_capacity = new_cap;
    return true;
}

bool mesh_slot_reserve_indices(mesh_slot_t *slot, uint32_t count) {
    if (!slot) { return false; }
    if (count > MESH_SLOT_MAX_INDICES) { return false; }
    if (count <= slot->index_capacity) { return true; }

    uint32_t new_cap = slot->index_capacity ? slot->index_capacity : 12;
    while (new_cap < count) { new_cap *= 2; }
    if (new_cap > MESH_SLOT_MAX_INDICES) { new_cap = MESH_SLOT_MAX_INDICES; }

    uint32_t *idx = realloc(slot->indices, (size_t)new_cap * sizeof(uint32_t));
    if (!idx) { return false; }
    /* Zero-fill new indices */
    if (new_cap > slot->index_capacity) {
        memset(idx + slot->index_capacity, 0,
               (size_t)(new_cap - slot->index_capacity) * sizeof(uint32_t));
    }
    slot->indices = idx;

    /* Grow polygroup buffer (one per face = capacity / 3 + 1) */
    uint32_t face_cap = new_cap / 3 + 1;
    uint16_t *pg = realloc(slot->polygroup_ids,
                           (size_t)face_cap * sizeof(uint16_t));
    if (!pg) { return false; }
    uint32_t old_face_cap = slot->index_capacity / 3 + 1;
    if (face_cap > old_face_cap) {
        memset(pg + old_face_cap, 0,
               (size_t)(face_cap - old_face_cap) * sizeof(uint16_t));
    }
    slot->polygroup_ids = pg;
    slot->index_capacity = new_cap;
    return true;
}
