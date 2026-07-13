/**
 * @file dmesh_load.c
 * @brief Dual-UV binary mesh loader with vertex welding (see dmesh_loader.h).
 *
 * The .dmesh file is a flat triangle soup: uint32 corner-count, then
 * count*(pos3,nrm3,uv0_2,uv1_2) floats. Coincident corners are welded into an
 * indexed mesh via an open-addressing hash, and tangents are generated from the
 * shared topology (reusing @ref obj_mesh_gen_tangents).
 */
#include "ferrum/mesh/dmesh_loader.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Floats per soup corner: pos(3) + normal(3) + uv0(2) + uv1(2). */
#define DMESH_STRIDE 10

static uint32_t dmesh_pow2_ceil(uint32_t x)
{
    uint32_t p = 1;
    while (p < x) p <<= 1;
    return p;
}

/* Order-independent hash over a corner's 10 attributes, quantised so that
 * near-identical corners collide into the same bucket (exact identity is then
 * confirmed by dmesh_corner_eq). */
static uint32_t dmesh_hash(const float *v, uint32_t mask)
{
    uint64_t h = 1469598103934665603ull; /* FNV-1a */
    for (int i = 0; i < DMESH_STRIDE; ++i) {
        int32_t q = (int32_t)lrintf(v[i] * 4096.0f);
        h ^= (uint64_t)(uint32_t)q;
        h *= 1099511628211ull;
    }
    return (uint32_t)(h >> 20) & mask;
}

/* True if unique vertex @p u of @p m equals soup corner @p s (within epsilon). */
static int dmesh_corner_eq(const obj_mesh_t *m, uint32_t u, const float *s)
{
    const float *p = &m->positions[u * 3], *n = &m->normals[u * 3];
    const float *a = &m->uvs[u * 2], *b = &m->uvs1[u * 2];
    return fabsf(p[0] - s[0]) < 1e-5f && fabsf(p[1] - s[1]) < 1e-5f &&
           fabsf(p[2] - s[2]) < 1e-5f && fabsf(n[0] - s[3]) < 1e-4f &&
           fabsf(n[1] - s[4]) < 1e-4f && fabsf(n[2] - s[5]) < 1e-4f &&
           fabsf(a[0] - s[6]) < 1e-5f && fabsf(a[1] - s[7]) < 1e-5f &&
           fabsf(b[0] - s[8]) < 1e-5f && fabsf(b[1] - s[9]) < 1e-5f;
}

int dmesh_load(const char *path, obj_mesh_t *out)
{
    if (path == NULL || out == NULL)
        return -1;
    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;
    uint32_t corners = 0;
    if (fread(&corners, sizeof(uint32_t), 1, fp) != 1 || corners == 0 ||
        corners % 3u != 0u) {
        fclose(fp);
        return -1;
    }
    float *soup = malloc((size_t)corners * DMESH_STRIDE * sizeof(float));
    if (!soup ||
        fread(soup, sizeof(float), (size_t)corners * DMESH_STRIDE, fp) !=
            (size_t)corners * DMESH_STRIDE) {
        free(soup);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* Weld corners into unique vertices (upper bound = corners). */
    out->positions = malloc((size_t)corners * 3 * sizeof(float));
    out->normals = malloc((size_t)corners * 3 * sizeof(float));
    out->uvs = malloc((size_t)corners * 2 * sizeof(float));
    out->uvs1 = malloc((size_t)corners * 2 * sizeof(float));
    out->indices = malloc((size_t)corners * sizeof(uint32_t));
    uint32_t hcap = dmesh_pow2_ceil(corners * 2u + 8u), hmask = hcap - 1u;
    uint32_t *slot = calloc(hcap, sizeof(uint32_t)); /* stores unique index + 1 */
    if (!out->positions || !out->normals || !out->uvs || !out->uvs1 ||
        !out->indices || !slot) {
        free(soup);
        free(slot);
        obj_mesh_free(out);
        return -1;
    }

    uint32_t uverts = 0;
    for (uint32_t i = 0; i < corners; ++i) {
        const float *s = &soup[(size_t)i * DMESH_STRIDE];
        uint32_t h = dmesh_hash(s, hmask), rep = 0xFFFFFFFFu;
        while (slot[h]) {
            if (dmesh_corner_eq(out, slot[h] - 1u, s)) {
                rep = slot[h] - 1u;
                break;
            }
            h = (h + 1u) & hmask;
        }
        if (rep == 0xFFFFFFFFu) {
            rep = uverts++;
            slot[h] = rep + 1u;
            memcpy(&out->positions[rep * 3], &s[0], 3 * sizeof(float));
            memcpy(&out->normals[rep * 3], &s[3], 3 * sizeof(float));
            memcpy(&out->uvs[rep * 2], &s[6], 2 * sizeof(float));
            memcpy(&out->uvs1[rep * 2], &s[8], 2 * sizeof(float));
        }
        out->indices[i] = rep;
    }
    out->vert_count = uverts;
    out->index_count = corners;

    free(soup);
    free(slot);
    if (obj_mesh_gen_tangents(out) != 0) {
        obj_mesh_free(out);
        return -1;
    }
    return 0;
}
