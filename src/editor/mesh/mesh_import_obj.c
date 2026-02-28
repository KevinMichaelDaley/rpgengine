/**
 * @file mesh_import_obj.c
 * @brief Import mesh from Wavefront OBJ format.
 *
 * Non-static functions (1 of 4): mesh_import_obj.
 *
 * Parses: v (positions), vn (normals), vt (UVs), f (faces).
 * Face format: v, v/vt, v/vt/vn, v//vn.
 */
#include "ferrum/editor/mesh/mesh_transfer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OBJ_MAX_LINE 512
#define OBJ_MAX_ELEMS 65536

/* ------------------------------------------------------------------ */
/* Static: temporary storage for OBJ data                              */
/* ------------------------------------------------------------------ */

typedef struct obj_data {
    float positions[OBJ_MAX_ELEMS * 3];
    float normals[OBJ_MAX_ELEMS * 3];
    float uvs[OBJ_MAX_ELEMS * 2];
    uint32_t pos_count;
    uint32_t nrm_count;
    uint32_t uv_count;
} obj_data_t;

/** Parse face vertex "v/vt/vn" or "v//vn" or "v/vt" or "v". */
static void parse_face_vert_(const char *s, int *vi, int *ti, int *ni) {
    *vi = 0; *ti = 0; *ni = 0;
    *vi = atoi(s);

    const char *slash1 = strchr(s, '/');
    if (!slash1) return;

    if (slash1[1] == '/') {
        /* v//vn */
        *ni = atoi(slash1 + 2);
    } else {
        *ti = atoi(slash1 + 1);
        const char *slash2 = strchr(slash1 + 1, '/');
        if (slash2) {
            *ni = atoi(slash2 + 1);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public: mesh_import_obj                                             */
/* ------------------------------------------------------------------ */

bool mesh_import_obj(mesh_slot_t *slot, const char *path) {
    if (!slot || !path) return false;

    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    obj_data_t *data = calloc(1, sizeof(obj_data_t));
    if (!data) { fclose(fp); return false; }

    mesh_slot_clear(slot);

    char line[OBJ_MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == 'v' && line[1] == ' ') {
            /* Position */
            if (data->pos_count < OBJ_MAX_ELEMS) {
                uint32_t i = data->pos_count;
                sscanf(line + 2, "%f %f %f",
                       &data->positions[i*3+0],
                       &data->positions[i*3+1],
                       &data->positions[i*3+2]);
                data->pos_count++;
            }
        } else if (line[0] == 'v' && line[1] == 'n') {
            /* Normal */
            if (data->nrm_count < OBJ_MAX_ELEMS) {
                uint32_t i = data->nrm_count;
                sscanf(line + 3, "%f %f %f",
                       &data->normals[i*3+0],
                       &data->normals[i*3+1],
                       &data->normals[i*3+2]);
                data->nrm_count++;
            }
        } else if (line[0] == 'v' && line[1] == 't') {
            /* UV */
            if (data->uv_count < OBJ_MAX_ELEMS) {
                uint32_t i = data->uv_count;
                sscanf(line + 3, "%f %f",
                       &data->uvs[i*2+0],
                       &data->uvs[i*2+1]);
                data->uv_count++;
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            /* Face — parse up to 4 verts (triangulate quads) */
            int vi[4], ti[4], ni[4];
            int count = 0;

            char *token = strtok(line + 2, " \t\r\n");
            while (token && count < 4) {
                parse_face_vert_(token, &vi[count], &ti[count], &ni[count]);
                count++;
                token = strtok(NULL, " \t\r\n");
            }

            if (count < 3) continue;

            /* Add vertices for this face */
            uint32_t indices[4];
            for (int j = 0; j < count; j++) {
                int pi = vi[j] - 1; /* OBJ is 1-indexed */
                if (pi < 0 || (uint32_t)pi >= data->pos_count) continue;

                float pos[3] = {
                    data->positions[pi*3+0],
                    data->positions[pi*3+1],
                    data->positions[pi*3+2]
                };
                float nrm[3] = {0, 0, 0};
                int nidx = ni[j] - 1;
                if (nidx >= 0 && (uint32_t)nidx < data->nrm_count) {
                    nrm[0] = data->normals[nidx*3+0];
                    nrm[1] = data->normals[nidx*3+1];
                    nrm[2] = data->normals[nidx*3+2];
                }

                indices[j] = mesh_slot_add_vertex(slot, pos, nrm);

                /* Set UV if available */
                int tidx = ti[j] - 1;
                if (tidx >= 0 && (uint32_t)tidx < data->uv_count && slot->uvs[0]) {
                    slot->uvs[0][indices[j]*2+0] = data->uvs[tidx*2+0];
                    slot->uvs[0][indices[j]*2+1] = data->uvs[tidx*2+1];
                }
            }

            /* Triangulate */
            mesh_slot_add_triangle(slot, indices[0], indices[1], indices[2], 0);
            if (count == 4) {
                mesh_slot_add_triangle(slot, indices[0], indices[2], indices[3], 0);
            }
        }
    }

    fclose(fp);
    free(data);
    return slot->index_count > 0;
}
