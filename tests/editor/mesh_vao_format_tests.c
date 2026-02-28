/**
 * @file mesh_vao_format_tests.c
 * @brief Tests for FVMA binary format: serialize, deserialize, round-trip.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_vao_format.h"

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

/* ------------------------------------------------------------------ */
/* Helper: build a simple triangle mesh                                */
/* ------------------------------------------------------------------ */

static void build_triangle_(mesh_slot_t *slot) {
    mesh_slot_init(slot, 4, 12);
    float p0[3] = {0, 0, 0}, p1[3] = {1, 0, 0}, p2[3] = {0, 1, 0};
    float n[3]  = {0, 0, 1};
    mesh_slot_add_vertex(slot, p0, n);
    mesh_slot_add_vertex(slot, p1, n);
    mesh_slot_add_vertex(slot, p2, n);
    mesh_slot_add_triangle(slot, 0, 1, 2, 0);
}

/* ------------------------------------------------------------------ */
/* Size calculation                                                    */
/* ------------------------------------------------------------------ */

static void test_size_calc(void) {
    mesh_slot_t slot;
    build_triangle_(&slot);

    uint32_t flags = MESH_VAO_FLAG_NORMALS;
    size_t expected = MESH_VAO_HEADER_SIZE
        + 3 * 12  /* positions: 3 verts * vec3 */
        + 3 * 12  /* normals: 3 verts * vec3 */
        + 3 * 4   /* indices: 3 * u32 */
        + 1 * 2;  /* polygroups: 1 face * u16 */

    size_t computed = mesh_vao_serialized_size(&slot, flags);
    ASSERT(computed == expected, "size matches expected");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_size_all_flags(void) {
    mesh_slot_t slot;
    build_triangle_(&slot);

    uint32_t flags = MESH_VAO_FLAG_NORMALS | MESH_VAO_FLAG_TANGENTS |
                     MESH_VAO_FLAG_UV0 | MESH_VAO_FLAG_UV1 |
                     MESH_VAO_FLAG_COLORS;
    size_t expected = MESH_VAO_HEADER_SIZE
        + 3 * 12  /* positions */
        + 3 * 12  /* normals */
        + 3 * 16  /* tangents */
        + 3 * 8   /* uv0 */
        + 3 * 8   /* uv1 */
        + 3 * 16  /* colors */
        + 3 * 4   /* indices */
        + 1 * 2;  /* polygroups */

    size_t computed = mesh_vao_serialized_size(&slot, flags);
    ASSERT(computed == expected, "all-flags size matches");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Round-trip: serialize then deserialize                               */
/* ------------------------------------------------------------------ */

static void test_round_trip_normals(void) {
    mesh_slot_t slot;
    build_triangle_(&slot);

    uint32_t flags = MESH_VAO_FLAG_NORMALS;
    size_t size = mesh_vao_serialized_size(&slot, flags);
    uint8_t *buf = malloc(size);
    ASSERT(buf != NULL, "alloc");

    size_t written = mesh_vao_serialize(&slot, flags, buf, size);
    ASSERT(written == size, "wrote expected bytes");

    mesh_slot_t out;
    bool ok = mesh_vao_deserialize(buf, size, &out);
    ASSERT(ok, "deserialize succeeded");
    ASSERT(out.vertex_count == 3, "3 vertices");
    ASSERT(out.index_count == 3, "3 indices");

    /* Check positions */
    for (int i = 0; i < 9; i++) {
        ASSERT(fabsf(out.positions[i] - slot.positions[i]) < 1e-6f, "pos match");
    }
    /* Check normals */
    for (int i = 0; i < 9; i++) {
        ASSERT(fabsf(out.normals[i] - slot.normals[i]) < 1e-6f, "nrm match");
    }
    /* Check indices */
    for (int i = 0; i < 3; i++) {
        ASSERT(out.indices[i] == slot.indices[i], "idx match");
    }
    /* Check polygroup */
    ASSERT(out.polygroup_ids[0] == slot.polygroup_ids[0], "pg match");

    free(buf);
    mesh_slot_destroy(&out);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_round_trip_minimal(void) {
    /* Minimal: positions + indices only (no flags) */
    mesh_slot_t slot;
    build_triangle_(&slot);

    uint32_t flags = 0;
    size_t size = mesh_vao_serialized_size(&slot, flags);
    uint8_t *buf = malloc(size);

    size_t written = mesh_vao_serialize(&slot, flags, buf, size);
    ASSERT(written == size, "wrote minimal");

    mesh_slot_t out;
    bool ok = mesh_vao_deserialize(buf, size, &out);
    ASSERT(ok, "deser minimal");
    ASSERT(out.vertex_count == 3, "3 verts");
    ASSERT(out.index_count == 3, "3 idx");

    /* Normals should be zeroed (not present in stream) */
    for (int i = 0; i < 9; i++) {
        ASSERT(fabsf(out.normals[i]) < 1e-6f, "normals zero");
    }

    free(buf);
    mesh_slot_destroy(&out);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_round_trip_all_attrs(void) {
    mesh_slot_t slot;
    build_triangle_(&slot);

    /* Set some UV and color data */
    slot.uvs[0][0] = 0.0f; slot.uvs[0][1] = 0.0f;
    slot.uvs[0][2] = 1.0f; slot.uvs[0][3] = 0.0f;
    slot.uvs[0][4] = 0.0f; slot.uvs[0][5] = 1.0f;

    slot.colors[0] = 1.0f; slot.colors[1] = 0.0f;
    slot.colors[2] = 0.0f; slot.colors[3] = 1.0f;

    uint32_t flags = MESH_VAO_FLAG_NORMALS | MESH_VAO_FLAG_TANGENTS |
                     MESH_VAO_FLAG_UV0 | MESH_VAO_FLAG_UV1 |
                     MESH_VAO_FLAG_COLORS;

    size_t size = mesh_vao_serialized_size(&slot, flags);
    uint8_t *buf = malloc(size);

    size_t written = mesh_vao_serialize(&slot, flags, buf, size);
    ASSERT(written == size, "wrote all attrs");

    mesh_slot_t out;
    bool ok = mesh_vao_deserialize(buf, size, &out);
    ASSERT(ok, "deser all attrs");

    /* UVs match */
    ASSERT(fabsf(out.uvs[0][2] - 1.0f) < 1e-6f, "uv0 match");
    /* Colors match */
    ASSERT(fabsf(out.colors[0] - 1.0f) < 1e-6f, "color match");

    free(buf);
    mesh_slot_destroy(&out);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Error cases                                                         */
/* ------------------------------------------------------------------ */

static void test_bad_magic(void) {
    mesh_slot_t slot;
    build_triangle_(&slot);

    uint32_t flags = MESH_VAO_FLAG_NORMALS;
    size_t size = mesh_vao_serialized_size(&slot, flags);
    uint8_t *buf = malloc(size);
    mesh_vao_serialize(&slot, flags, buf, size);

    /* Corrupt the magic */
    buf[0] = 'X';

    mesh_slot_t out;
    bool ok = mesh_vao_deserialize(buf, size, &out);
    ASSERT(!ok, "bad magic should fail");

    free(buf);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_truncated(void) {
    mesh_slot_t slot;
    build_triangle_(&slot);

    uint32_t flags = MESH_VAO_FLAG_NORMALS;
    size_t size = mesh_vao_serialized_size(&slot, flags);
    uint8_t *buf = malloc(size);
    mesh_vao_serialize(&slot, flags, buf, size);

    mesh_slot_t out;
    /* Too short for header */
    bool ok = mesh_vao_deserialize(buf, 10, &out);
    ASSERT(!ok, "truncated header should fail");

    /* Header OK but data truncated */
    ok = mesh_vao_deserialize(buf, MESH_VAO_HEADER_SIZE + 4, &out);
    ASSERT(!ok, "truncated data should fail");

    free(buf);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_null_args(void) {
    ASSERT(mesh_vao_serialized_size(NULL, 0) == 0, "null slot size");
    ASSERT(mesh_vao_serialize(NULL, 0, NULL, 0) == 0, "null serialize");

    mesh_slot_t out;
    ASSERT(!mesh_vao_deserialize(NULL, 0, &out), "null buf deser");
    ASSERT(!mesh_vao_deserialize(NULL, 100, NULL), "all null deser");

    g_pass++;
}

static void test_empty_mesh(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 0, 0);

    uint32_t flags = 0;
    size_t size = mesh_vao_serialized_size(&slot, flags);
    ASSERT(size == MESH_VAO_HEADER_SIZE, "empty mesh is just header");

    uint8_t *buf = malloc(size);
    size_t written = mesh_vao_serialize(&slot, flags, buf, size);
    ASSERT(written == size, "wrote empty");

    mesh_slot_t out;
    bool ok = mesh_vao_deserialize(buf, size, &out);
    ASSERT(ok, "deser empty");
    ASSERT(out.vertex_count == 0, "0 verts");
    ASSERT(out.index_count == 0, "0 idx");

    free(buf);
    mesh_slot_destroy(&out);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(void) {
    printf("mesh_vao_format_tests:\n");

    test_size_calc();
    test_size_all_flags();
    test_round_trip_normals();
    test_round_trip_minimal();
    test_round_trip_all_attrs();
    test_bad_magic();
    test_truncated();
    test_null_args();
    test_empty_mesh();

    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
