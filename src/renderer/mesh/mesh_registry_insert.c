/**
 * @file mesh_registry_insert.c
 * @brief Insert static and skeletal meshes into a mesh_registry_t.
 */

#include "ferrum/renderer/mesh/mesh_registry.h"

#include <string.h>

/* ── Pop a free slot (shared helper) ──────────────────────────────── */

static int pop_free_slot_(mesh_registry_t *reg, uint32_t *out_index)
{
    if (reg->freelist_count == 0) {
        return MESH_REGISTRY_ERR_FULL;
    }
    --reg->freelist_count;
    *out_index = reg->freelist[reg->freelist_count];
    return MESH_REGISTRY_OK;
}

/* ── mesh_registry_insert_static ──────────────────────────────────── */

int mesh_registry_insert_static(mesh_registry_t *reg,
                                const static_mesh_create_info_t *info,
                                mesh_handle_t *out)
{
    if (!reg || !info || !out) {
        return MESH_REGISTRY_ERR_INVALID;
    }

    uint32_t slot;
    int rc = pop_free_slot_(reg, &slot);
    if (rc != MESH_REGISTRY_OK) { return rc; }

    /* Create the static mesh directly into the slot. */
    memset(&reg->meshes[slot].stat, 0, sizeof(static_mesh_t));
    rc = static_mesh_create(reg->loader, info, &reg->meshes[slot].stat);
    if (rc != STATIC_MESH_OK) {
        /* Return slot to freelist. */
        reg->freelist[reg->freelist_count++] = slot;
        return MESH_REGISTRY_ERR_GL;
    }

    reg->types[slot] = MESH_TYPE_STATIC;
    ++reg->count;

    out->index      = slot;
    out->generation = reg->generations[slot];
    return MESH_REGISTRY_OK;
}

/* ── mesh_registry_insert_skeletal ────────────────────────────────── */

int mesh_registry_insert_skeletal(mesh_registry_t *reg,
                                  const skeletal_mesh_create_info_t *info,
                                  mesh_handle_t *out)
{
    if (!reg || !info || !out) {
        return MESH_REGISTRY_ERR_INVALID;
    }

    uint32_t slot;
    int rc = pop_free_slot_(reg, &slot);
    if (rc != MESH_REGISTRY_OK) { return rc; }

    memset(&reg->meshes[slot].skel, 0, sizeof(skeletal_mesh_t));
    rc = skeletal_mesh_create(reg->loader, info, &reg->meshes[slot].skel);
    if (rc != SKELETAL_MESH_OK) {
        reg->freelist[reg->freelist_count++] = slot;
        return MESH_REGISTRY_ERR_GL;
    }

    reg->types[slot] = MESH_TYPE_SKELETAL;
    ++reg->count;

    out->index      = slot;
    out->generation = reg->generations[slot];
    return MESH_REGISTRY_OK;
}
