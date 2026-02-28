/**
 * @file client_mesh_render.c
 * @brief Deserialize FVMA into CPU render data.
 */
#include "ferrum/editor/client/client_mesh_render.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_slot.h"

#include <stdlib.h>
#include <string.h>

bool client_mesh_data_from_fvma(client_mesh_data_t *out,
                                const uint8_t *fvma_buf, size_t fvma_size) {
    if (!out || !fvma_buf || fvma_size == 0) return false;
    memset(out, 0, sizeof(*out));

    /* Deserialize into a temporary mesh_slot_t */
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    if (!mesh_vao_deserialize(fvma_buf, fvma_size, &slot)) {
        return false;
    }

    /* Copy positions */
    out->vertex_count = slot.vertex_count;
    out->index_count = slot.index_count;

    out->positions = malloc(slot.vertex_count * 3 * sizeof(float));
    if (!out->positions) { mesh_slot_destroy(&slot); return false; }
    memcpy(out->positions, slot.positions, slot.vertex_count * 3 * sizeof(float));

    /* Copy normals if present */
    if (slot.normals) {
        out->normals = malloc(slot.vertex_count * 3 * sizeof(float));
        if (out->normals) {
            memcpy(out->normals, slot.normals, slot.vertex_count * 3 * sizeof(float));
        }
    }

    /* Copy indices */
    out->indices = malloc(slot.index_count * sizeof(uint32_t));
    if (!out->indices) {
        free(out->positions); free(out->normals);
        mesh_slot_destroy(&slot);
        memset(out, 0, sizeof(*out));
        return false;
    }
    memcpy(out->indices, slot.indices, slot.index_count * sizeof(uint32_t));

    mesh_slot_destroy(&slot);
    return true;
}

void client_mesh_data_destroy(client_mesh_data_t *data) {
    if (!data) return;
    free(data->positions);
    free(data->normals);
    free(data->indices);
    memset(data, 0, sizeof(*data));
}
