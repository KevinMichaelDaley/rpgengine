/**
 * @file mesh_export_obj.c
 * @brief Export mesh to Wavefront OBJ format.
 *
 * Non-static functions (1 of 4): mesh_export_obj.
 */
#include "ferrum/editor/mesh/mesh_transfer.h"

#include <stdio.h>

bool mesh_export_obj(const mesh_slot_t *slot, const char *path) {
    if (!slot || !path) return false;

    FILE *fp = fopen(path, "w");
    if (!fp) return false;

    fprintf(fp, "# Ferrum mesh export\n");

    /* Vertices */
    for (uint32_t v = 0; v < slot->vertex_count; v++) {
        fprintf(fp, "v %.6f %.6f %.6f\n",
                slot->positions[v*3+0],
                slot->positions[v*3+1],
                slot->positions[v*3+2]);
    }

    /* Normals */
    for (uint32_t v = 0; v < slot->vertex_count; v++) {
        fprintf(fp, "vn %.6f %.6f %.6f\n",
                slot->normals[v*3+0],
                slot->normals[v*3+1],
                slot->normals[v*3+2]);
    }

    /* UVs (channel 0) */
    if (slot->uvs[0]) {
        for (uint32_t v = 0; v < slot->vertex_count; v++) {
            fprintf(fp, "vt %.6f %.6f\n",
                    slot->uvs[0][v*2+0],
                    slot->uvs[0][v*2+1]);
        }
    }

    /* Faces (1-indexed) */
    uint32_t fc = slot->index_count / 3;
    for (uint32_t f = 0; f < fc; f++) {
        uint32_t i0 = slot->indices[f*3+0] + 1;
        uint32_t i1 = slot->indices[f*3+1] + 1;
        uint32_t i2 = slot->indices[f*3+2] + 1;
        if (slot->uvs[0]) {
            fprintf(fp, "f %u/%u/%u %u/%u/%u %u/%u/%u\n",
                    i0, i0, i0, i1, i1, i1, i2, i2, i2);
        } else {
            fprintf(fp, "f %u//%u %u//%u %u//%u\n",
                    i0, i0, i1, i1, i2, i2);
        }
    }

    fclose(fp);
    return true;
}
