/**
 * @file mesh_registry_init.c
 * @brief Lifecycle for mesh_registry_t: init and destroy.
 */

#include "ferrum/renderer/mesh/mesh_registry.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ── mesh_registry_init ───────────────────────────────────────────── */

int mesh_registry_init(mesh_registry_t *reg, uint32_t capacity,
                       const gl_loader_t *loader)
{
    if (!reg || !loader || capacity == 0) {
        return MESH_REGISTRY_ERR_INVALID;
    }

    /*
     * Single allocation for all internal arrays:
     *   types[]       — capacity × sizeof(mesh_type_t)
     *   meshes[]      — capacity × sizeof(union mesh_slot)
     *   generations[] — capacity × sizeof(uint16_t)
     *   freelist[]    — capacity × sizeof(uint32_t)
     */
    size_t sz_types = (size_t)capacity * sizeof(mesh_type_t);
    size_t sz_meshes = (size_t)capacity * sizeof(union mesh_slot);
    size_t sz_gens  = (size_t)capacity * sizeof(uint16_t);
    size_t sz_free  = (size_t)capacity * sizeof(uint32_t);
    size_t total    = sz_types + sz_meshes + sz_gens + sz_free;

    uint8_t *block = (uint8_t *)malloc(total);
    if (!block) {
        return MESH_REGISTRY_ERR_OOM;
    }
    memset(block, 0, total);

    uint8_t *ptr = block;
    reg->types       = (mesh_type_t *)ptr;   ptr += sz_types;
    reg->meshes      = (union mesh_slot *)ptr; ptr += sz_meshes;
    reg->generations = (uint16_t *)ptr;      ptr += sz_gens;
    reg->freelist    = (uint32_t *)ptr;

    /* Build freelist: push slots in reverse so slot 0 is popped first. */
    for (uint32_t i = 0; i < capacity; ++i) {
        reg->freelist[i] = capacity - 1 - i;
    }
    reg->freelist_count = capacity;
    reg->count          = 0;
    reg->capacity       = capacity;
    reg->loader         = loader;

    return MESH_REGISTRY_OK;
}

/* ── mesh_registry_destroy ────────────────────────────────────────── */

void mesh_registry_destroy(mesh_registry_t *reg)
{
    if (!reg || !reg->types) { return; }

    /* Destroy every live mesh. */
    for (uint32_t i = 0; i < reg->capacity; ++i) {
        if (reg->types[i] == MESH_TYPE_STATIC) {
            static_mesh_destroy(&reg->meshes[i].stat);
        } else if (reg->types[i] == MESH_TYPE_SKELETAL) {
            skeletal_mesh_destroy(&reg->meshes[i].skel);
        }
    }

    /* Free the single backing block (types is at offset 0). */
    free(reg->types);

    reg->types       = NULL;
    reg->meshes      = NULL;
    reg->generations = NULL;
    reg->freelist    = NULL;
    reg->count       = 0;
    reg->capacity    = 0;
}
