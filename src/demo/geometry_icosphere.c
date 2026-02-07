#include "ferrum/demo/geometry.h"
#include <math.h>
#include <string.h>

/* ---------- constants ---------- */

/* Golden ratio. */
static const float PHI = 1.6180339887498949f;

/* Icosahedron: 12 base vertices, 20 faces.
 * After 2 subdivisions: 20 * 4^2 = 320 faces, 320 * 3 verts * 3 floats = 2880 floats. */
#define ICO_BASE_VERTS 12
#define ICO_BASE_FACES 20
#define SUBDIV_LEVELS  2

/* Max faces after subdivision: 20 * 4^2 = 320. */
#define MAX_FACES (ICO_BASE_FACES * 16)  /* 320 */
#define MAX_FLOATS (MAX_FACES * 9)       /* 2880 */

/* ---------- helpers ---------- */

/** Normalize a vec3 in-place to unit length. */
static void normalize(float *v) {
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.0f) {
        float inv = 1.0f / len;
        v[0] *= inv;
        v[1] *= inv;
        v[2] *= inv;
    }
}

/** Compute the midpoint of two vec3s, normalize to unit sphere. */
static void midpoint_normalize(const float *a, const float *b, float *out) {
    out[0] = (a[0] + b[0]) * 0.5f;
    out[1] = (a[1] + b[1]) * 0.5f;
    out[2] = (a[2] + b[2]) * 0.5f;
    normalize(out);
}

/* ---------- icosahedron seed data ---------- */

/**
 * Populate the initial icosahedron (20 faces as flat triangle list).
 * Returns number of floats written (20 * 9 = 180).
 */
static uint32_t seed_icosahedron(float *buf) {
    /* 12 vertices of a unit icosahedron (normalized). */
    float verts[ICO_BASE_VERTS][3] = {
        {-1,  PHI, 0}, { 1,  PHI, 0}, {-1, -PHI, 0}, { 1, -PHI, 0},
        { 0, -1,  PHI}, { 0,  1,  PHI}, { 0, -1, -PHI}, { 0,  1, -PHI},
        { PHI, 0, -1}, { PHI, 0,  1}, {-PHI, 0, -1}, {-PHI, 0,  1},
    };
    for (int i = 0; i < ICO_BASE_VERTS; i++) {
        normalize(verts[i]);
    }

    /* 20 triangular faces (vertex indices). */
    static const int faces[ICO_BASE_FACES][3] = {
        { 0, 11,  5}, { 0,  5,  1}, { 0,  1,  7}, { 0,  7, 10}, { 0, 10, 11},
        { 1,  5,  9}, { 5, 11,  4}, {11, 10,  2}, {10,  7,  6}, { 7,  1,  8},
        { 3,  9,  4}, { 3,  4,  2}, { 3,  2,  6}, { 3,  6,  8}, { 3,  8,  9},
        { 4,  9,  5}, { 2,  4, 11}, { 6,  2, 10}, { 8,  6,  7}, { 9,  8,  1},
    };

    uint32_t offset = 0;
    for (int f = 0; f < ICO_BASE_FACES; f++) {
        for (int v = 0; v < 3; v++) {
            int idx = faces[f][v];
            buf[offset++] = verts[idx][0];
            buf[offset++] = verts[idx][1];
            buf[offset++] = verts[idx][2];
        }
    }
    return offset;  /* 180 */
}

/* ---------- subdivision ---------- */

/**
 * Subdivide all triangles in src into dst (each triangle → 4).
 * Both buffers hold flat triangle lists (9 floats per triangle).
 * Returns the number of floats written to dst.
 */
static uint32_t subdivide(const float *src, uint32_t src_floats, float *dst) {
    uint32_t num_tris = src_floats / 9;
    uint32_t dst_offset = 0;

    for (uint32_t t = 0; t < num_tris; t++) {
        const float *a = &src[t * 9 + 0];
        const float *b = &src[t * 9 + 3];
        const float *c = &src[t * 9 + 6];

        /* Midpoints of each edge, projected onto unit sphere. */
        float ab[3], bc[3], ca[3];
        midpoint_normalize(a, b, ab);
        midpoint_normalize(b, c, bc);
        midpoint_normalize(c, a, ca);

        /* Triangle 1: a, ab, ca */
        memcpy(&dst[dst_offset], a, 3 * sizeof(float));  dst_offset += 3;
        memcpy(&dst[dst_offset], ab, 3 * sizeof(float)); dst_offset += 3;
        memcpy(&dst[dst_offset], ca, 3 * sizeof(float)); dst_offset += 3;

        /* Triangle 2: b, bc, ab */
        memcpy(&dst[dst_offset], b, 3 * sizeof(float));  dst_offset += 3;
        memcpy(&dst[dst_offset], bc, 3 * sizeof(float)); dst_offset += 3;
        memcpy(&dst[dst_offset], ab, 3 * sizeof(float)); dst_offset += 3;

        /* Triangle 3: c, ca, bc */
        memcpy(&dst[dst_offset], c, 3 * sizeof(float));  dst_offset += 3;
        memcpy(&dst[dst_offset], ca, 3 * sizeof(float)); dst_offset += 3;
        memcpy(&dst[dst_offset], bc, 3 * sizeof(float)); dst_offset += 3;

        /* Triangle 4: ab, bc, ca */
        memcpy(&dst[dst_offset], ab, 3 * sizeof(float)); dst_offset += 3;
        memcpy(&dst[dst_offset], bc, 3 * sizeof(float)); dst_offset += 3;
        memcpy(&dst[dst_offset], ca, 3 * sizeof(float)); dst_offset += 3;
    }
    return dst_offset;
}

/* ---------- public API ---------- */

int demo_generate_icosphere(float *out_vertices, uint32_t *out_count) {
    if (!out_count) return -1;

    /* Report required size if caller is querying. */
    if (!out_vertices) {
        *out_count = MAX_FLOATS;
        return 0;
    }

    /* Two scratch buffers for ping-pong subdivision. */
    float buf_a[MAX_FLOATS];
    float buf_b[MAX_FLOATS];

    /* Seed with base icosahedron. */
    uint32_t current_floats = seed_icosahedron(buf_a);
    float *src = buf_a;
    float *dst = buf_b;

    /* Subdivide twice. */
    for (int level = 0; level < SUBDIV_LEVELS; level++) {
        current_floats = subdivide(src, current_floats, dst);
        /* Swap buffers. */
        float *tmp = src;
        src = dst;
        dst = tmp;
    }

    /* Copy result to output. src points to the final data. */
    memcpy(out_vertices, src, current_floats * sizeof(float));
    *out_count = current_floats;
    return 0;
}
