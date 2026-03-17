/**
 * @file static_mesh_primitives.c
 * @brief Generate primitive meshes (box, sphere, capsule, plane).
 *
 * Each generator builds vertex/index arrays on the stack or heap,
 * then calls static_mesh_create() to upload them.
 */

#include "ferrum/renderer/mesh/static_mesh.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Box ─────────────────────────────────────────────────────────── */

int static_mesh_create_box(const gl_loader_t *loader,
                           float width, float height, float depth,
                           static_mesh_t *out)
{
    if (!loader || !out) return STATIC_MESH_ERR_INVALID;

    float half_x = width  * 0.5f;
    float half_y = height * 0.5f;
    float half_z = depth  * 0.5f;

    /* 24 vertices (4 per face, 6 faces), 36 indices. */
    float positions[24 * 3];
    float normals[24 * 3];
    float uvs[24 * 2];
    uint32_t indices[36];

    /* Face definitions: 6 faces × 4 vertices. */
    static const float face_normals[6][3] = {
        { 0, 0, 1}, { 0, 0,-1}, { 1, 0, 0},
        {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}
    };
    /* Sign patterns for each face's 4 vertices (x,y relative to face). */
    static const int face_signs[6][4][3] = {
        /* +Z */ {{ 1, 1, 1},{-1, 1, 1},{-1,-1, 1},{ 1,-1, 1}},
        /* -Z */ {{-1, 1,-1},{ 1, 1,-1},{ 1,-1,-1},{-1,-1,-1}},
        /* +X */ {{ 1, 1,-1},{ 1, 1, 1},{ 1,-1, 1},{ 1,-1,-1}},
        /* -X */ {{-1, 1, 1},{-1, 1,-1},{-1,-1,-1},{-1,-1, 1}},
        /* +Y */ {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}},
        /* -Y */ {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}}
    };
    static const float face_uvs[4][2] = {
        {0,1},{1,1},{1,0},{0,0}
    };

    for (int f = 0; f < 6; f++) {
        for (int v = 0; v < 4; v++) {
            int idx = f * 4 + v;
            positions[idx * 3 + 0] = face_signs[f][v][0] * half_x;
            positions[idx * 3 + 1] = face_signs[f][v][1] * half_y;
            positions[idx * 3 + 2] = face_signs[f][v][2] * half_z;
            normals[idx * 3 + 0] = face_normals[f][0];
            normals[idx * 3 + 1] = face_normals[f][1];
            normals[idx * 3 + 2] = face_normals[f][2];
            uvs[idx * 2 + 0] = face_uvs[v][0];
            uvs[idx * 2 + 1] = face_uvs[v][1];
        }
        int base = f * 4;
        int ti   = f * 6;
        indices[ti + 0] = (uint32_t)base;
        indices[ti + 1] = (uint32_t)(base + 1);
        indices[ti + 2] = (uint32_t)(base + 2);
        indices[ti + 3] = (uint32_t)base;
        indices[ti + 4] = (uint32_t)(base + 2);
        indices[ti + 5] = (uint32_t)(base + 3);
    }

    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.positions    = positions;
    info.normals      = normals;
    info.uv0          = uvs;
    info.indices      = indices;
    info.vertex_count = 24;
    info.index_count  = 36;

    return static_mesh_create(loader, &info, out);
}

/* ── Plane ───────────────────────────────────────────────────────── */

int static_mesh_create_plane(const gl_loader_t *loader,
                             float half_w, float half_d,
                             static_mesh_t *out)
{
    if (!loader || !out) return STATIC_MESH_ERR_INVALID;

    /* 4 vertices, 6 indices (2 triangles). */
    float positions[4 * 3] = {
        -half_w, 0.0f, -half_d,
         half_w, 0.0f, -half_d,
         half_w, 0.0f,  half_d,
        -half_w, 0.0f,  half_d
    };
    float normals[4 * 3] = {
        0, 1, 0,  0, 1, 0,  0, 1, 0,  0, 1, 0
    };
    float uvs[4 * 2] = {
        0, 0,  1, 0,  1, 1,  0, 1
    };
    uint32_t indices[6] = { 0, 2, 1, 0, 3, 2 };

    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.positions    = positions;
    info.normals      = normals;
    info.uv0          = uvs;
    info.indices      = indices;
    info.vertex_count = 4;
    info.index_count  = 6;

    return static_mesh_create(loader, &info, out);
}

/* ── Sphere ──────────────────────────────────────────────────────── */

int static_mesh_create_sphere(const gl_loader_t *loader,
                              float radius,
                              uint32_t slices, uint32_t rings,
                              static_mesh_t *out)
{
    if (!loader || !out) return STATIC_MESH_ERR_INVALID;
    if (slices < 3) slices = 3;
    if (rings  < 2) rings  = 2;

    uint32_t vert_count = (rings + 1) * (slices + 1);
    uint32_t idx_count  = rings * slices * 6;

    float    *positions = (float *)malloc(vert_count * 3 * sizeof(float));
    float    *normals   = (float *)malloc(vert_count * 3 * sizeof(float));
    float    *uvs       = (float *)malloc(vert_count * 2 * sizeof(float));
    uint32_t *indices   = (uint32_t *)malloc(idx_count * sizeof(uint32_t));
    if (!positions || !normals || !uvs || !indices) {
        free(positions); free(normals); free(uvs); free(indices);
        return STATIC_MESH_ERR_OOM;
    }

    /* Generate vertices. */
    uint32_t vi = 0;
    for (uint32_t r = 0; r <= rings; r++) {
        float phi = (float)M_PI * (float)r / (float)rings;
        float sp  = sinf(phi);
        float cp  = cosf(phi);
        for (uint32_t s = 0; s <= slices; s++) {
            float theta = 2.0f * (float)M_PI * (float)s / (float)slices;
            float st    = sinf(theta);
            float ct    = cosf(theta);

            float nx = sp * ct;
            float ny = cp;
            float nz = sp * st;

            normals[vi * 3 + 0]   = nx;
            normals[vi * 3 + 1]   = ny;
            normals[vi * 3 + 2]   = nz;
            positions[vi * 3 + 0] = radius * nx;
            positions[vi * 3 + 1] = radius * ny;
            positions[vi * 3 + 2] = radius * nz;
            uvs[vi * 2 + 0] = (float)s / (float)slices;
            uvs[vi * 2 + 1] = (float)r / (float)rings;
            vi++;
        }
    }

    /* Generate indices. */
    uint32_t ii = 0;
    for (uint32_t r = 0; r < rings; r++) {
        for (uint32_t s = 0; s < slices; s++) {
            uint32_t a = r * (slices + 1) + s;
            uint32_t b = a + slices + 1;
            indices[ii++] = a;
            indices[ii++] = b;
            indices[ii++] = a + 1;
            indices[ii++] = a + 1;
            indices[ii++] = b;
            indices[ii++] = b + 1;
        }
    }

    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.positions    = positions;
    info.normals      = normals;
    info.uv0          = uvs;
    info.indices      = indices;
    info.vertex_count = vert_count;
    info.index_count  = idx_count;

    int rc = static_mesh_create(loader, &info, out);
    free(positions); free(normals); free(uvs); free(indices);
    return rc;
}

/* ── Capsule ─────────────────────────────────────────────────────── */

int static_mesh_create_capsule(const gl_loader_t *loader,
                               float radius, float half_height,
                               uint32_t slices, uint32_t cap_rings,
                               static_mesh_t *out)
{
    if (!loader || !out) return STATIC_MESH_ERR_INVALID;
    if (slices    < 3) slices    = 3;
    if (cap_rings < 1) cap_rings = 1;

    /* Topology: top cap + cylinder body + bottom cap.
     * Total rings = cap_rings (top) + 1 (cylinder) + cap_rings (bottom).
     */
    uint32_t total_rings = cap_rings * 2 + 1;
    uint32_t vert_count  = (total_rings + 1) * (slices + 1);
    uint32_t idx_count   = total_rings * slices * 6;

    float    *positions = (float *)malloc(vert_count * 3 * sizeof(float));
    float    *normals   = (float *)malloc(vert_count * 3 * sizeof(float));
    uint32_t *indices   = (uint32_t *)malloc(idx_count * sizeof(uint32_t));
    if (!positions || !normals || !indices) {
        free(positions); free(normals); free(indices);
        return STATIC_MESH_ERR_OOM;
    }

    uint32_t vi = 0;

    /* Top cap hemisphere (pole at y = half_height + radius). */
    for (uint32_t r = 0; r <= cap_rings; r++) {
        float phi = (float)M_PI * 0.5f * (float)r / (float)cap_rings;
        float sp  = sinf(phi);
        float cp  = cosf(phi);
        for (uint32_t s = 0; s <= slices; s++) {
            float theta = 2.0f * (float)M_PI * (float)s / (float)slices;
            float nx = sp * cosf(theta);
            float ny = cp;
            float nz = sp * sinf(theta);
            normals[vi * 3 + 0]   = nx;
            normals[vi * 3 + 1]   = ny;
            normals[vi * 3 + 2]   = nz;
            positions[vi * 3 + 0] = radius * nx;
            positions[vi * 3 + 1] = half_height + radius * ny;
            positions[vi * 3 + 2] = radius * nz;
            vi++;
        }
    }

    /* Bottom cap hemisphere (pole at y = -(half_height + radius)). */
    for (uint32_t r = 1; r <= cap_rings; r++) {
        float phi = (float)M_PI * 0.5f
                  + (float)M_PI * 0.5f * (float)r / (float)cap_rings;
        float sp  = sinf(phi);
        float cp  = cosf(phi);
        for (uint32_t s = 0; s <= slices; s++) {
            float theta = 2.0f * (float)M_PI * (float)s / (float)slices;
            float nx = sp * cosf(theta);
            float ny = cp;
            float nz = sp * sinf(theta);
            normals[vi * 3 + 0]   = nx;
            normals[vi * 3 + 1]   = ny;
            normals[vi * 3 + 2]   = nz;
            positions[vi * 3 + 0] = radius * nx;
            positions[vi * 3 + 1] = -half_height + radius * ny;
            positions[vi * 3 + 2] = radius * nz;
            vi++;
        }
    }

    /* Generate indices. */
    uint32_t ii = 0;
    for (uint32_t r = 0; r < total_rings; r++) {
        for (uint32_t s = 0; s < slices; s++) {
            uint32_t a = r * (slices + 1) + s;
            uint32_t b = a + slices + 1;
            indices[ii++] = a;
            indices[ii++] = b;
            indices[ii++] = a + 1;
            indices[ii++] = a + 1;
            indices[ii++] = b;
            indices[ii++] = b + 1;
        }
    }

    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.positions    = positions;
    info.normals      = normals;
    info.indices      = indices;
    info.vertex_count = vi;
    info.index_count  = ii;

    int rc = static_mesh_create(loader, &info, out);
    free(positions); free(normals); free(indices);
    return rc;
}
