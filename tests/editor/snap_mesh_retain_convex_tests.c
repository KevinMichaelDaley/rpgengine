/**
 * @file snap_mesh_retain_convex_tests.c
 * @brief Tests for convex hull and compound snap mesh generation.
 */

#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/convex_compound.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ASSERT(expr) do { \
    if (!(expr)) { \
        printf("  FAIL: %s (line %d)\n", #expr, __LINE__); \
        return 0; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps))

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) printf("OK   %s\n", #fn); \
    else { printf("FAIL %s\n", #fn); fails++; } \
    total++; \
} while (0)

/**
 * Helper: build a tetrahedron hull for testing.
 * 4 vertices, 4 triangular faces.
 */
static void build_tetrahedron_(phys_convex_hull_t *hull) {
    memset(hull, 0, sizeof(*hull));

    hull->vertices[0] = (phys_vec3_t){ 1, 0, -0.707f};
    hull->vertices[1] = (phys_vec3_t){-1, 0, -0.707f};
    hull->vertices[2] = (phys_vec3_t){ 0, 1,  0.707f};
    hull->vertices[3] = (phys_vec3_t){ 0,-1,  0.707f};
    hull->vertex_count = 4;

    /* 4 triangular faces. Each face has 3 indices. */
    uint16_t face_indices[] = {
        0, 1, 2,    /* face 0 */
        0, 2, 3,    /* face 1 */
        1, 3, 2,    /* face 2 */
        0, 3, 1,    /* face 3 */
    };
    memcpy(hull->indices, face_indices, sizeof(face_indices));
    hull->index_count = 12;

    for (int f = 0; f < 4; ++f) {
        hull->faces[f].index_start = (uint16_t)(f * 3);
        hull->faces[f].index_count = 3;
        hull->faces[f].normal = (phys_vec3_t){0, 1, 0}; /* approximate */
    }
    hull->face_count = 4;
}

/**
 * Helper: build a cube hull with 6 quad faces (4 verts each).
 * 8 vertices, 6 faces, 24 total face indices.
 */
static void build_cube_hull_(phys_convex_hull_t *hull) {
    memset(hull, 0, sizeof(*hull));

    /* 8 cube corners. */
    hull->vertices[0] = (phys_vec3_t){-0.5f, -0.5f, -0.5f};
    hull->vertices[1] = (phys_vec3_t){ 0.5f, -0.5f, -0.5f};
    hull->vertices[2] = (phys_vec3_t){ 0.5f,  0.5f, -0.5f};
    hull->vertices[3] = (phys_vec3_t){-0.5f,  0.5f, -0.5f};
    hull->vertices[4] = (phys_vec3_t){-0.5f, -0.5f,  0.5f};
    hull->vertices[5] = (phys_vec3_t){ 0.5f, -0.5f,  0.5f};
    hull->vertices[6] = (phys_vec3_t){ 0.5f,  0.5f,  0.5f};
    hull->vertices[7] = (phys_vec3_t){-0.5f,  0.5f,  0.5f};
    hull->vertex_count = 8;

    /* 6 quad faces (CCW from outside). */
    uint16_t face_indices[] = {
        0, 3, 2, 1,  /* back  (-Z) */
        4, 5, 6, 7,  /* front (+Z) */
        0, 1, 5, 4,  /* bottom (-Y) */
        2, 3, 7, 6,  /* top   (+Y) */
        0, 4, 7, 3,  /* left  (-X) */
        1, 2, 6, 5,  /* right (+X) */
    };
    memcpy(hull->indices, face_indices, sizeof(face_indices));
    hull->index_count = 24;

    phys_vec3_t normals[] = {
        { 0,  0, -1},  /* back */
        { 0,  0,  1},  /* front */
        { 0, -1,  0},  /* bottom */
        { 0,  1,  0},  /* top */
        {-1,  0,  0},  /* left */
        { 1,  0,  0},  /* right */
    };
    for (int f = 0; f < 6; ++f) {
        hull->faces[f].index_start = (uint16_t)(f * 4);
        hull->faces[f].index_count = 4;
        hull->faces[f].normal = normals[f];
    }
    hull->face_count = 6;
}

/* ---- Tests ---- */

/** Tetrahedron: 4 triangular faces → 4 triangles, 12 indices. */
static int test_convex_hull_tetrahedron(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);

    phys_convex_hull_t hull;
    build_tetrahedron_(&hull);

    snap_mesh_retain_convex_hull(&cache, 0, &hull);

    const snap_mesh_t *mesh = snap_mesh_cache_get(&cache, 0);
    ASSERT(mesh != NULL);
    ASSERT(mesh->vertex_count == 4);
    ASSERT(mesh->index_count == 12);  /* 4 triangular faces × 3 */
    ASSERT(mesh->positions != NULL);
    ASSERT(mesh->normals != NULL);
    ASSERT(mesh->indices != NULL);

    /* Verify first vertex position matches hull. */
    ASSERT_NEAR(mesh->positions[0], 1.0f, 0.01f);
    ASSERT_NEAR(mesh->positions[1], 0.0f, 0.01f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Cube: 6 quad faces → 12 triangles (2 per quad), 36 indices. */
static int test_convex_hull_cube(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);

    phys_convex_hull_t hull;
    build_cube_hull_(&hull);

    snap_mesh_retain_convex_hull(&cache, 0, &hull);

    const snap_mesh_t *mesh = snap_mesh_cache_get(&cache, 0);
    ASSERT(mesh != NULL);
    ASSERT(mesh->vertex_count == 8);
    /* 6 quad faces × 2 triangles × 3 indices = 36 */
    ASSERT(mesh->index_count == 36);

    /* All vertex positions should be at ±0.5. */
    for (uint32_t v = 0; v < 8; ++v) {
        ASSERT(fabsf(mesh->positions[v * 3 + 0]) < 0.51f);
        ASSERT(fabsf(mesh->positions[v * 3 + 1]) < 0.51f);
        ASSERT(fabsf(mesh->positions[v * 3 + 2]) < 0.51f);
    }

    /* Normals should be unit length. */
    for (uint32_t v = 0; v < 8; ++v) {
        float nx = mesh->normals[v * 3 + 0];
        float ny = mesh->normals[v * 3 + 1];
        float nz = mesh->normals[v * 3 + 2];
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        ASSERT_NEAR(len, 1.0f, 0.01f);
    }

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Null hull should not crash. */
static int test_convex_hull_null_safety(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);

    snap_mesh_retain_convex_hull(&cache, 0, NULL);
    ASSERT(snap_mesh_cache_get(&cache, 0) == NULL);

    snap_mesh_retain_convex_hull(NULL, 0, NULL);

    /* Empty hull (0 verts) should be rejected. */
    phys_convex_hull_t empty;
    memset(&empty, 0, sizeof(empty));
    snap_mesh_retain_convex_hull(&cache, 0, &empty);
    ASSERT(snap_mesh_cache_get(&cache, 0) == NULL);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Compound with 2 tetrahedra → merged mesh. */
static int test_compound_two_hulls(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);

    phys_convex_hull_t hulls[2];
    build_tetrahedron_(&hulls[0]);
    build_tetrahedron_(&hulls[1]);

    /* Offset second hull's vertices. */
    for (uint32_t v = 0; v < hulls[1].vertex_count; ++v) {
        hulls[1].vertices[v].x += 3.0f;
    }

    phys_convex_compound_t compound;
    memset(&compound, 0, sizeof(compound));
    compound.child_hull_indices[0] = 0;
    compound.child_hull_indices[1] = 1;
    compound.child_count = 2;

    snap_mesh_retain_compound(&cache, 0, hulls, &compound);

    const snap_mesh_t *mesh = snap_mesh_cache_get(&cache, 0);
    ASSERT(mesh != NULL);
    /* 2 tetrahedra: 4 + 4 = 8 vertices. */
    ASSERT(mesh->vertex_count == 8);
    /* 2 × 12 indices = 24. */
    ASSERT(mesh->index_count == 24);

    /* Second hull's first vertex should be offset by 3. */
    ASSERT_NEAR(mesh->positions[4 * 3 + 0], 1.0f + 3.0f, 0.01f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Compound with null/empty inputs should not crash. */
static int test_compound_null_safety(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);

    snap_mesh_retain_compound(&cache, 0, NULL, NULL);
    ASSERT(snap_mesh_cache_get(&cache, 0) == NULL);

    phys_convex_compound_t empty;
    memset(&empty, 0, sizeof(empty));
    phys_convex_hull_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    snap_mesh_retain_compound(&cache, 0, &dummy, &empty);
    ASSERT(snap_mesh_cache_get(&cache, 0) == NULL);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Cube hull built via phys_convex_hull_build from point cloud. */
static int test_convex_hull_from_build(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);

    /* 8 box corners as input point cloud. */
    phys_vec3_t points[8] = {
        {-0.5f, -0.5f, -0.5f},
        { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f},
        {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f},
        { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f},
    };

    phys_convex_hull_t hull;
    int rc = phys_convex_hull_build(&hull, points, 8);
    ASSERT(rc == 0);
    ASSERT(hull.vertex_count == 8);
    ASSERT(hull.face_count == 6);  /* Cube has 6 faces after coplanar merge. */

    snap_mesh_retain_convex_hull(&cache, 0, &hull);

    const snap_mesh_t *mesh = snap_mesh_cache_get(&cache, 0);
    ASSERT(mesh != NULL);
    ASSERT(mesh->vertex_count == 8);
    /* 6 quad faces × 2 tris × 3 = 36 indices. */
    ASSERT(mesh->index_count == 36);

    printf("  hull_from_build: verts=%u faces=%u idx=%u\n",
           mesh->vertex_count, hull.face_count, mesh->index_count);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/* ---- Main ---- */

int main(void) {
    int fails = 0, total = 0;

    RUN(test_convex_hull_tetrahedron);
    RUN(test_convex_hull_cube);
    RUN(test_convex_hull_null_safety);
    RUN(test_compound_two_hulls);
    RUN(test_compound_null_safety);
    RUN(test_convex_hull_from_build);

    printf("\n%d / %d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
