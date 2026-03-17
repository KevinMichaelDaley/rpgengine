/**
 * @file snap_ray_triangle_tests.c
 * @brief Tests for snap ray-triangle and ray-mesh intersection.
 */

#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/editor/viewport/snap/snap_raycast.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"

#include <stdio.h>
#include <math.h>

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

/* ---- snap_ray_vs_triangle tests ---- */

/** Ray straight down hits triangle in XZ plane at Y=0. */
static int test_ray_hit_centered(void) {
    vec3_t v0 = {-1, 0, -1};
    vec3_t v1 = { 1, 0, -1};
    vec3_t v2 = { 0, 0,  1};
    vec3_t origin = {0, 5, 0};
    vec3_t dir = {0, -1, 0};
    float t;

    ASSERT(snap_ray_vs_triangle(origin, dir, v0, v1, v2, &t));
    ASSERT_NEAR(t, 5.0f, 0.01f);
    return 1;
}

/** Ray misses triangle entirely. */
static int test_ray_miss(void) {
    vec3_t v0 = {-1, 0, -1};
    vec3_t v1 = { 1, 0, -1};
    vec3_t v2 = { 0, 0,  1};
    vec3_t origin = {10, 5, 0};  /* Far off to the side. */
    vec3_t dir = {0, -1, 0};
    float t;

    ASSERT(!snap_ray_vs_triangle(origin, dir, v0, v1, v2, &t));
    return 1;
}

/** Ray behind origin (triangle in opposite direction). */
static int test_ray_behind(void) {
    vec3_t v0 = {-1, 0, -1};
    vec3_t v1 = { 1, 0, -1};
    vec3_t v2 = { 0, 0,  1};
    vec3_t origin = {0, -5, 0};  /* Below the triangle. */
    vec3_t dir = {0, -1, 0};     /* Pointing further down. */
    float t;

    ASSERT(!snap_ray_vs_triangle(origin, dir, v0, v1, v2, &t));
    return 1;
}

/** Ray parallel to triangle plane. */
static int test_ray_parallel(void) {
    vec3_t v0 = {-1, 0, -1};
    vec3_t v1 = { 1, 0, -1};
    vec3_t v2 = { 0, 0,  1};
    vec3_t origin = {0, 1, 0};
    vec3_t dir = {1, 0, 0};  /* Along X, parallel to XZ plane. */
    float t;

    ASSERT(!snap_ray_vs_triangle(origin, dir, v0, v1, v2, &t));
    return 1;
}

/** Ray hits at edge (grazing). */
static int test_ray_edge_hit(void) {
    vec3_t v0 = {0, 0, 0};
    vec3_t v1 = {1, 0, 0};
    vec3_t v2 = {0, 0, 1};
    vec3_t origin = {0.5f, 3.0f, 0.0f};  /* Above edge v0-v1. */
    vec3_t dir = {0, -1, 0};
    float t;

    ASSERT(snap_ray_vs_triangle(origin, dir, v0, v1, v2, &t));
    ASSERT_NEAR(t, 3.0f, 0.01f);
    return 1;
}

/** Long-distance hit — t > 1 must work (unlike physics CCD). */
static int test_ray_long_distance(void) {
    vec3_t v0 = {-1, 0, -1};
    vec3_t v1 = { 1, 0, -1};
    vec3_t v2 = { 0, 0,  1};
    vec3_t origin = {0, 100, 0};
    vec3_t dir = {0, -1, 0};
    float t;

    ASSERT(snap_ray_vs_triangle(origin, dir, v0, v1, v2, &t));
    ASSERT_NEAR(t, 100.0f, 0.01f);
    return 1;
}

/* ---- snap_ray_vs_snap_mesh tests ---- */

/** Build a 2-triangle mesh (a quad in XZ plane at Y=0). */
static void make_quad_mesh(snap_mesh_t *mesh) {
    static float positions[] = {
        -1, 0, -1,
         1, 0, -1,
         1, 0,  1,
        -1, 0,  1
    };
    static float normals[] = {
        0, 1, 0,
        0, 1, 0,
        0, 1, 0,
        0, 1, 0
    };
    static uint32_t indices[] = {
        0, 1, 2,
        0, 2, 3
    };
    mesh->positions = positions;
    mesh->normals = normals;
    mesh->indices = indices;
    mesh->vertex_count = 4;
    mesh->index_count = 6;
}

/** Ray hits the mesh quad — returns nearest triangle. */
static int test_mesh_hit_nearest(void) {
    snap_mesh_t mesh;
    make_quad_mesh(&mesh);

    vec3_t origin = {0.5f, 5.0f, 0.5f};
    vec3_t dir = {0, -1, 0};
    mat4_t model = mat4_identity();
    float t;
    uint32_t face_idx;
    vec3_t hit_normal;

    ASSERT(snap_ray_vs_snap_mesh(origin, dir, &mesh, &model,
                                  &t, &face_idx, &hit_normal));
    ASSERT_NEAR(t, 5.0f, 0.01f);
    /* Should hit second triangle (0,2,3) since point is in +X,+Z quadrant. */
    ASSERT(face_idx == 0 || face_idx == 1);
    /* Normal depends on winding; quad winding gives -Y or +Y. */
    ASSERT(fabsf(hit_normal.y) > 0.9f);
    return 1;
}

/** Ray misses mesh entirely. */
static int test_mesh_miss(void) {
    snap_mesh_t mesh;
    make_quad_mesh(&mesh);

    vec3_t origin = {5, 5, 5};  /* Far away. */
    vec3_t dir = {0, -1, 0};
    mat4_t model = mat4_identity();
    float t;
    uint32_t face_idx;
    vec3_t hit_normal;

    ASSERT(!snap_ray_vs_snap_mesh(origin, dir, &mesh, &model,
                                   &t, &face_idx, &hit_normal));
    return 1;
}

/** Mesh with model transform (translated up by 3). */
static int test_mesh_with_transform(void) {
    snap_mesh_t mesh;
    make_quad_mesh(&mesh);

    vec3_t origin = {0, 10, 0};
    vec3_t dir = {0, -1, 0};
    mat4_t model = mat4_translation(0, 3, 0);
    float t;
    uint32_t face_idx;
    vec3_t hit_normal;

    ASSERT(snap_ray_vs_snap_mesh(origin, dir, &mesh, &model,
                                  &t, &face_idx, &hit_normal));
    /* Hit should be at Y=3 (translated), so t = 10-3 = 7. */
    ASSERT_NEAR(t, 7.0f, 0.01f);
    return 1;
}

/** Mesh with scale transform (2x scale). */
static int test_mesh_with_scale(void) {
    snap_mesh_t mesh;
    make_quad_mesh(&mesh);

    /* Quad is [-1,1] in XZ. Scaled 2x, it becomes [-2,2].
     * Ray at x=1.5 should hit the scaled mesh but not the unscaled one. */
    vec3_t origin = {1.5f, 5, 0};
    vec3_t dir = {0, -1, 0};
    mat4_t model = mat4_scaling(2, 1, 2);
    float t;
    uint32_t face_idx;
    vec3_t hit_normal;

    ASSERT(snap_ray_vs_snap_mesh(origin, dir, &mesh, &model,
                                  &t, &face_idx, &hit_normal));
    ASSERT_NEAR(t, 5.0f, 0.01f);

    /* Without scale, should miss. */
    mat4_t identity = mat4_identity();
    ASSERT(!snap_ray_vs_snap_mesh(origin, dir, &mesh, &identity,
                                   &t, &face_idx, &hit_normal));
    return 1;
}

/* ---- Main ---- */

int main(void) {
    int total = 0, fails = 0;
    printf("=== snap_ray_triangle tests ===\n");

    RUN(test_ray_hit_centered);
    RUN(test_ray_miss);
    RUN(test_ray_behind);
    RUN(test_ray_parallel);
    RUN(test_ray_edge_hit);
    RUN(test_ray_long_distance);
    RUN(test_mesh_hit_nearest);
    RUN(test_mesh_miss);
    RUN(test_mesh_with_transform);
    RUN(test_mesh_with_scale);

    printf("\n%d passed, %d failed\n", total - fails, fails);
    return fails > 0 ? 1 : 0;
}
