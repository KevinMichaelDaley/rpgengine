/**
 * @file obj_loader.c
 * @brief Wavefront OBJ triangle mesh loader implementation.
 *
 * Parses .obj files line-by-line, collecting vertex positions and
 * emitting triangle vertices for each face line.  Only triangle
 * faces (3 indices per face) are supported.
 */

#include "ferrum/mesh/obj_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal: parse one face index ─────────────────────────────── */

/**
 * @brief Parse a face index token like "3", "3//1", "3/2/1", or "3/2".
 *
 * Only extracts the vertex position index (1-based).
 *
 * @param token  Null-terminated token string.
 * @return 1-based vertex index, or 0 on parse failure.
 */
static uint32_t parse_face_vertex(const char *token) {
    /* strtoul stops at '/' or end of string — exactly what we need. */
    char *end = NULL;
    unsigned long idx = strtoul(token, &end, 10);
    if (end == token || idx == 0) {
        return 0;
    }
    return (uint32_t)idx;
}

/* ── Internal: two-pass vertex loader ──────────────────────────── */

/**
 * @brief Count vertex and face lines in an OBJ file.
 *
 * @param fp          Open file handle (rewound to start).
 * @param out_verts   Receives vertex count.
 * @param out_faces   Receives face (triangle) count.
 */
static void count_elements(FILE *fp, uint32_t *out_verts, uint32_t *out_faces) {
    char line[512];
    uint32_t verts = 0;
    uint32_t faces = 0;

    while (fgets(line, (int)sizeof(line), fp)) {
        if (line[0] == 'v' && line[1] == ' ') {
            verts++;
        } else if (line[0] == 'f' && line[1] == ' ') {
            faces++;
        }
    }

    *out_verts = verts;
    *out_faces = faces;
}

/* ── Public API ─────────────────────────────────────────────────── */

int obj_load_triangles(const char *path,
                       float scale,
                       float *verts_out,
                       uint32_t max_tris,
                       uint32_t *out_tri_count) {
    if (!path || !out_tri_count) {
        return -1;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    /* Pass 1: count vertices and faces. */
    uint32_t vert_count = 0;
    uint32_t face_count = 0;
    count_elements(fp, &vert_count, &face_count);

    *out_tri_count = face_count;

    /* Check capacity. */
    if (face_count > max_tris || !verts_out) {
        fclose(fp);
        return (face_count == 0 && !verts_out) ? 0 : -1;
    }

    /* Allocate vertex position array for random access by face indices. */
    float *positions = (float *)malloc((size_t)vert_count * 3 * sizeof(float));
    if (!positions) {
        fclose(fp);
        return -1;
    }

    /* Pass 2: read vertices and faces. */
    rewind(fp);

    char line[512];
    uint32_t vi = 0;   /* vertex write index */
    uint32_t fi = 0;   /* face (triangle) write index */

    while (fgets(line, (int)sizeof(line), fp)) {
        if (line[0] == 'v' && line[1] == ' ') {
            /* Parse vertex position: "v x y z" */
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) {
                positions[vi * 3 + 0] = x * scale;
                positions[vi * 3 + 1] = y * scale;
                positions[vi * 3 + 2] = z * scale;
                vi++;
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            /* Parse face: "f idx0 idx1 idx2" (various formats). */
            char t0[64] = {0}, t1[64] = {0}, t2[64] = {0};
            if (sscanf(line + 2, "%63s %63s %63s", t0, t1, t2) == 3) {
                uint32_t i0 = parse_face_vertex(t0);
                uint32_t i1 = parse_face_vertex(t1);
                uint32_t i2 = parse_face_vertex(t2);
                if (i0 == 0 || i1 == 0 || i2 == 0 ||
                    i0 > vert_count || i1 > vert_count || i2 > vert_count) {
                    continue; /* skip malformed face */
                }
                /* Convert to 0-based. */
                i0--; i1--; i2--;

                float *dst = verts_out + (size_t)fi * 9;
                memcpy(dst + 0, positions + (size_t)i0 * 3, 3 * sizeof(float));
                memcpy(dst + 3, positions + (size_t)i1 * 3, 3 * sizeof(float));
                memcpy(dst + 6, positions + (size_t)i2 * 3, 3 * sizeof(float));
                fi++;
            }
        }
    }

    free(positions);
    fclose(fp);

    *out_tri_count = fi;
    return 0;
}
