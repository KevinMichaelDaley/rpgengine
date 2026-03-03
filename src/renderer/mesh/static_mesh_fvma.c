/**
 * @file static_mesh_fvma.c
 * @brief Create a static mesh from FVMA binary data.
 *
 * Deserializes an FVMA blob via mesh_vao_deserialize(), then converts
 * the resulting mesh_slot_t into a static_mesh_t via static_mesh_create().
 */

#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_slot.h"

#include <string.h>

int static_mesh_create_from_fvma(const gl_loader_t *loader,
                                 const uint8_t *fvma_data,
                                 size_t fvma_size,
                                 static_mesh_t *out)
{
    if (!loader || !fvma_data || fvma_size == 0 || !out) {
        return STATIC_MESH_ERR_INVALID;
    }

    /* Deserialize into a temporary mesh_slot_t.
     * mesh_vao_deserialize() calls mesh_slot_init() internally,
     * so we just zero-init the struct here. */
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));

    if (!mesh_vao_deserialize(fvma_data, fvma_size, &slot)) {
        return STATIC_MESH_ERR_FORMAT;
    }

    /* Build create info from the slot. */
    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.positions    = slot.positions;
    info.normals      = slot.normals;
    info.tangents     = slot.tangents;
    info.uv0          = slot.uvs[0];
    info.uv1          = slot.uvs[1];
    info.colors       = slot.colors;
    info.indices      = slot.indices;
    info.vertex_count = slot.vertex_count;
    info.index_count  = slot.index_count;

    int rc = static_mesh_create(loader, &info, out);
    mesh_slot_destroy(&slot);
    return rc;
}
