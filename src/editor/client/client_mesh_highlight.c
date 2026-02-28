/**
 * @file client_mesh_highlight.c
 * @brief Generate selection highlight index buffers for mesh faces.
 */
#include "ferrum/editor/client/client_mesh_render.h"

#include <stdlib.h>
#include <string.h>

bool client_mesh_face_highlight(const uint32_t *indices,
                                uint32_t index_count,
                                const mesh_sel_bitset_t *sel_faces,
                                uint32_t **out_indices,
                                uint32_t *out_count) {
    if (!indices || !sel_faces || !out_indices || !out_count) return false;
    if (index_count < 3) return false;

    uint32_t face_count = index_count / 3;

    /* Count selected faces */
    uint32_t sel_count = 0;
    for (uint32_t f = 0; f < face_count; f++) {
        if (mesh_sel_bitset_test(sel_faces, f)) sel_count++;
    }

    if (sel_count == 0) {
        *out_indices = NULL;
        *out_count = 0;
        return true;
    }

    /* Allocate highlight indices: 3 per selected face */
    uint32_t *hi = malloc(sel_count * 3 * sizeof(uint32_t));
    if (!hi) return false;

    uint32_t idx = 0;
    for (uint32_t f = 0; f < face_count; f++) {
        if (mesh_sel_bitset_test(sel_faces, f)) {
            hi[idx++] = indices[f * 3 + 0];
            hi[idx++] = indices[f * 3 + 1];
            hi[idx++] = indices[f * 3 + 2];
        }
    }

    *out_indices = hi;
    *out_count = sel_count * 3;
    return true;
}
