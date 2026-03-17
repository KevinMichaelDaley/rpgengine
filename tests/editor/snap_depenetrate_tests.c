/**
 * @file snap_depenetrate_tests.c
 * @brief Tests for snap depenetration (plane projection + mesh-mesh).
 */

#include "ferrum/editor/viewport/snap/snap_depenetrate.h"
#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/vec4.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/mat4.h"

#include <stdio.h>
#include <string.h>
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

/* ── Plane depenetration tests ─────────────────────────────────── */

/**
 * Unit box placed at surface point with normal up.
 * Box center is on the plane, so bottom half penetrates by 0.5.
 */
static int test_plane_box_on_surface(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);

    /* Box at origin, surface plane at origin with normal (0,1,0).
     * Bottom vertices at y=-0.5, so depth = 0.5. */
    mat4_t model = mat4_identity();
    vec3_t point = {0, 0, 0};
    vec3_t normal = {0, 1, 0};

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_plane(
        snap_mesh_cache_get(&cache, 0), &model,
        point, normal, &result);

    ASSERT(hit);
    ASSERT_NEAR(result.penetration, 0.5f, 0.01f);
    /* Push should be (0, 0.5, 0). */
    ASSERT_NEAR(result.push.x, 0.0f, 0.01f);
    ASSERT_NEAR(result.push.y, 0.5f, 0.01f);
    ASSERT_NEAR(result.push.z, 0.0f, 0.01f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Box already above the surface should not need push. */
static int test_plane_box_above_surface(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);

    /* Box at (0, 2, 0), surface at origin with normal up.
     * Lowest vertex at y=1.5, well above. */
    mat4_t model = mat4_translation(0, 2, 0);
    vec3_t point = {0, 0, 0};
    vec3_t normal = {0, 1, 0};

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_plane(
        snap_mesh_cache_get(&cache, 0), &model,
        point, normal, &result);

    ASSERT(!hit);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Angled surface normal should project correctly. */
static int test_plane_angled_normal(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);

    /* Box at origin, surface at (0, -0.3, 0) with normal pointing
     * at 45° between +Y and +X: (1/√2, 1/√2, 0). */
    mat4_t model = mat4_identity();
    float inv_sqrt2 = 1.0f / sqrtf(2.0f);
    vec3_t point = {0, -0.3f, 0};
    vec3_t normal = {inv_sqrt2, inv_sqrt2, 0};

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_plane(
        snap_mesh_cache_get(&cache, 0), &model,
        point, normal, &result);

    ASSERT(hit);
    ASSERT(result.penetration > 0.0f);
    /* Push direction should be along the normal. */
    float push_dot = result.push.x * normal.x + result.push.y * normal.y;
    ASSERT(push_dot > 0.0f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Scaled box should penetrate deeper. */
static int test_plane_scaled_box(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);

    /* 2x scaled box at origin on surface at origin, normal up.
     * Bottom at y=-1, so depth = 1.0. */
    mat4_t model = mat4_scaling(2.0f, 2.0f, 2.0f);
    vec3_t point = {0, 0, 0};
    vec3_t normal = {0, 1, 0};

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_plane(
        snap_mesh_cache_get(&cache, 0), &model,
        point, normal, &result);

    ASSERT(hit);
    ASSERT_NEAR(result.penetration, 1.0f, 0.01f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Null inputs should not crash. */
static int test_plane_null_safety(void) {
    mat4_t identity = mat4_identity();
    vec3_t zero = {0, 0, 0};
    vec3_t up = {0, 1, 0};
    snap_depenetrate_result_t result;

    ASSERT(!snap_depenetrate_plane(NULL, &identity, zero, up, &result));
    ASSERT(!snap_depenetrate_plane(NULL, NULL, zero, up, NULL));
    return 1;
}

/* ── Triangle-vs-triangle SAT depenetration tests ─────────────── */

/**
 * Helper: transform a snap_mesh_t into world-space triangle vertices.
 * Returns triangle count. out_verts must hold mesh->index_count vec3_t.
 */
static uint32_t mesh_to_world_tris_(const snap_mesh_t *mesh,
                                       const mat4_t *model,
                                       vec3_t *out_verts) {
    uint32_t tri_count = mesh->index_count / 3;
    for (uint32_t t = 0; t < tri_count; ++t) {
        for (int v = 0; v < 3; ++v) {
            uint32_t idx = mesh->indices[t * 3 + v];
            vec4_t local = {
                mesh->positions[idx * 3 + 0],
                mesh->positions[idx * 3 + 1],
                mesh->positions[idx * 3 + 2],
                1.0f
            };
            vec4_t world = mat4_mul_vec4(*model, local);
            out_verts[t * 3 + v] = (vec3_t){world.x, world.y, world.z};
        }
    }
    return tri_count;
}

/** Two overlapping boxes — face-face crossing detected by SAT. */
static int test_sat_crossing_boxes(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);
    snap_mesh_retain_box(&cache, 1);

    mat4_t model_a = mat4_identity();
    const snap_mesh_t *mesh_a = snap_mesh_cache_get(&cache, 0);

    vec3_t env_verts[256];
    uint32_t env_count = mesh_to_world_tris_(mesh_a, &model_a, env_verts);

    /* Entity box offset to overlap. */
    mat4_t model_b = mat4_translation(0.3f, 0.3f, 0.0f);
    const snap_mesh_t *mesh_b = snap_mesh_cache_get(&cache, 1);

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_vs_tris(mesh_b, &model_b,
                                          env_verts, env_count, &result);

    ASSERT(hit);
    ASSERT(result.penetration > 0.0f);
    float push_len = sqrtf(result.push.x * result.push.x +
                           result.push.y * result.push.y +
                           result.push.z * result.push.z);
    ASSERT(push_len > 0.0f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Separated boxes — SAT finds no intersection. */
static int test_sat_separated(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);
    snap_mesh_retain_box(&cache, 1);

    mat4_t model_a = mat4_identity();
    const snap_mesh_t *mesh_a = snap_mesh_cache_get(&cache, 0);

    vec3_t env_verts[256];
    uint32_t env_count = mesh_to_world_tris_(mesh_a, &model_a, env_verts);

    mat4_t model_b = mat4_translation(3.0f, 0.0f, 0.0f);
    const snap_mesh_t *mesh_b = snap_mesh_cache_get(&cache, 1);

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_vs_tris(mesh_b, &model_b,
                                          env_verts, env_count, &result);

    ASSERT(!hit);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Box sitting on large floor triangle — SAT detects face crossing. */
static int test_sat_floor_triangle(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);

    /* Large floor triangle at y=0 with normal +Y.
     * cross(b-a, c-a) = cross((10,0,0),(5,0,-10)) = (0,100,0) → +Y. */
    vec3_t env_verts[3] = {
        {-5.0f, 0.0f,  5.0f},
        { 5.0f, 0.0f,  5.0f},
        { 0.0f, 0.0f, -5.0f}
    };

    /* Box at origin — side faces cross the floor at y=0. */
    mat4_t model_b = mat4_identity();
    const snap_mesh_t *mesh_b = snap_mesh_cache_get(&cache, 0);

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_vs_tris(mesh_b, &model_b,
                                          env_verts, 1, &result);

    ASSERT(hit);
    ASSERT(result.penetration > 0.0f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/**
 * Edge-edge crossing: a rotated box whose edge crosses an env box edge.
 * This case is missed by vertex-vs-face but caught by SAT edge cross axes.
 */
static int test_sat_edge_crossing(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);
    snap_mesh_retain_box(&cache, 1);

    /* Env box at origin. */
    mat4_t model_a = mat4_identity();
    const snap_mesh_t *mesh_a = snap_mesh_cache_get(&cache, 0);

    vec3_t env_verts[256];
    uint32_t env_count = mesh_to_world_tris_(mesh_a, &model_a, env_verts);

    /* Entity box rotated 45° around Y, positioned so its edge crosses
     * the env box edge. The rotation causes edge-edge intersection
     * that vertex-vs-face would miss. */
    quat_t rot45 = quat_from_axis_angle(
        (vec3_t){0, 1, 0}, 0.7854f, 1e-6f);  /* 45° around Y */
    mat4_t rot_mat;
    quat_to_mat4(rot45, &rot_mat);
    mat4_t trans = mat4_translation(0.6f, 0.0f, 0.0f);
    mat4_t model_b = mat4_mul(trans, rot_mat);
    const snap_mesh_t *mesh_b = snap_mesh_cache_get(&cache, 1);

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_vs_tris(mesh_b, &model_b,
                                          env_verts, env_count, &result);

    ASSERT(hit);
    ASSERT(result.penetration > 0.0f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/**
 * Containment: small env triangle entirely inside large entity box.
 * No surfaces cross, but vertex-vs-face (reverse) detects the env
 * vertices behind entity faces.
 */
static int test_containment_env_inside_entity(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);

    /* Large entity box (2x scale, extends -1 to +1). */
    snap_mesh_retain_box(&cache, 0);
    mat4_t model_b = mat4_scaling(2.0f, 2.0f, 2.0f);
    const snap_mesh_t *mesh_b = snap_mesh_cache_get(&cache, 0);

    /* Small env triangle at y=0.5 entirely inside the box.
     * Env vertices are behind entity top face (y=+1). */
    vec3_t env_verts[3] = {
        {-0.1f, 0.5f,  0.1f},
        { 0.1f, 0.5f,  0.1f},
        { 0.0f, 0.5f, -0.1f}
    };

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_vs_tris(mesh_b, &model_b,
                                          env_verts, 1, &result);

    ASSERT(hit);
    ASSERT(result.penetration > 0.0f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/**
 * Capsule hemisphere hanging over box edge — the real-world scenario.
 * Capsule is positioned so its center is at the box top face edge,
 * and the hemisphere hangs over the side. The hemisphere triangles
 * cross the box side face.
 */
static int test_capsule_on_box_edge(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);      /* env box at origin */
    snap_mesh_retain_capsule(&cache, 1);  /* entity capsule */

    /* Box at origin (faces at ±0.5). */
    mat4_t model_a = mat4_identity();
    const snap_mesh_t *mesh_a = snap_mesh_cache_get(&cache, 0);

    vec3_t env_verts[256];
    uint32_t env_count = mesh_to_world_tris_(mesh_a, &model_a, env_verts);

    /* Capsule placed at (0.4, 0.5, 0) — center on the box top face,
     * near the +X edge. After plane push the capsule center would be at
     * (0.4, 1.0, 0) but we test the intermediate state where center is
     * at (0.4, 0.5, 0) — hemisphere extends below y=0.5 and beyond x=0.5. */
    mat4_t model_b = mat4_translation(0.4f, 0.5f, 0.0f);
    const snap_mesh_t *mesh_b = snap_mesh_cache_get(&cache, 1);

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_vs_tris(mesh_b, &model_b,
                                          env_verts, env_count, &result);

    ASSERT(hit);
    ASSERT(result.penetration > 0.0f);
    printf("  capsule_on_box_edge: pen=%.4f push=(%.4f, %.4f, %.4f)\n",
           result.penetration, result.push.x, result.push.y, result.push.z);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/**
 * Capsule center ON box surface, AFTER plane push.
 * Capsule center is at (0, 1.0, 0), so bottom pole is at (0, 0.5, 0)
 * which is exactly the box top face. Hemisphere vertices near the bottom
 * should be just above the surface. But if capsule hangs over the edge
 * horizontally, side faces might still cross.
 */
static int test_capsule_after_plane_push(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);
    snap_mesh_retain_capsule(&cache, 1);

    mat4_t model_a = mat4_identity();
    const snap_mesh_t *mesh_a = snap_mesh_cache_get(&cache, 0);

    vec3_t env_verts[256];
    uint32_t env_count = mesh_to_world_tris_(mesh_a, &model_a, env_verts);

    /* Capsule near box edge, after plane push. Center at (0.4, 1.0, 0).
     * Capsule bottom pole at (0.4, 0.5, 0). Hemisphere extends in XZ
     * by radius 0.25, so its rightmost points are at x=0.65 — past the
     * box edge at x=0.5. These points should be behind the box's +X face. */
    mat4_t model_b = mat4_translation(0.4f, 1.0f, 0.0f);
    const snap_mesh_t *mesh_b = snap_mesh_cache_get(&cache, 1);

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_vs_tris(mesh_b, &model_b,
                                          env_verts, env_count, &result);

    /* If capsule extends past box edge, there should be some overlap. */
    printf("  capsule_after_push: hit=%d pen=%.4f push=(%.4f, %.4f, %.4f)\n",
           hit, result.penetration, result.push.x, result.push.y, result.push.z);

    /* The capsule at x=0.4 with radius 0.25 extends to x=0.65.
     * Box right face is at x=0.5. Hemisphere vertices at x>0.5 and
     * y<0.5 are behind the right face. This should be detected. */
    ASSERT(hit);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/**
 * Box placed at (0.3, 0.5, 0) — bottom face at y=0, overlapping env box
 * in X direction. After plane push would be at (0.3, 1.0, 0) — bottom
 * at y=0.5 (flush with env top). Should have no overlap.
 * Then nudge it to (0.6, 1.0, 0) so it overhangs the +X edge.
 * Bottom vertices at (1.1, 0.5, ±0.5) are past x=0.5 but at y=0.5
 * which is the env top face. No containment expected.
 */
static int test_box_push_direction(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);  /* env */
    snap_mesh_retain_box(&cache, 1);  /* entity */

    mat4_t model_a = mat4_identity();
    const snap_mesh_t *mesh_a = snap_mesh_cache_get(&cache, 0);
    vec3_t env_verts[256];
    uint32_t env_count = mesh_to_world_tris_(mesh_a, &model_a, env_verts);

    /* Entity box slightly overlapping at (0.8, 0.0, 0).
     * Entity extends from x=0.3 to x=1.3 in X.
     * Env box from x=-0.5 to x=0.5.
     * Overlap in X: 0.3..0.5.
     * Entity vertices at x=0.3 are at signed_dist_right = 0.3-0.5 = -0.2
     * behind right face. Min pen for x=0.3 vertex through right face = 0.2.
     * Push should be in +X direction (right face normal). */
    mat4_t model_b = mat4_translation(0.8f, 0.0f, 0.0f);
    const snap_mesh_t *mesh_b = snap_mesh_cache_get(&cache, 1);

    snap_depenetrate_result_t result;
    bool hit = snap_depenetrate_vs_tris(mesh_b, &model_b,
                                          env_verts, env_count, &result);

    printf("  box_push_direction: hit=%d pen=%.4f push=(%.4f, %.4f, %.4f)\n",
           hit, result.penetration, result.push.x, result.push.y, result.push.z);

    ASSERT(hit);
    ASSERT(result.penetration > 0.0f);
    /* Push should be predominantly in +X (push entity right out of env). */
    ASSERT(result.push.x > 0.0f);
    /* Y and Z push should be much smaller than X push. */
    ASSERT(fabsf(result.push.x) > fabsf(result.push.y));

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/**
 * Test iterative convergence: realistic scenario — entity box on top of
 * env box, slightly offset so it hangs over the edge. After initial
 * plane push (manually applied here), iterative depenetrate should
 * resolve the edge overlap.
 */
static int test_iterative_convergence(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);  /* env */
    snap_mesh_retain_box(&cache, 1);  /* entity */

    mat4_t model_a = mat4_identity();
    const snap_mesh_t *mesh_a = snap_mesh_cache_get(&cache, 0);
    vec3_t env_verts[256];
    uint32_t env_count = mesh_to_world_tris_(mesh_a, &model_a, env_verts);

    const snap_mesh_t *mesh_b = snap_mesh_cache_get(&cache, 1);

    /* Entity box at (0.8, 0.0, 0.0): overlapping in X only.
     * Entity extends x=[0.3, 1.3], env x=[-0.5, 0.5].
     * Overlap region x=[0.3, 0.5], depth = 0.2. */
    float pos_x = 0.8f, pos_y = 0.0f, pos_z = 0.0f;

    int converged_at = -1;
    for (int iter = 0; iter < 64; ++iter) {
        mat4_t model_b = mat4_translation(pos_x, pos_y, pos_z);
        snap_depenetrate_result_t dep;
        if (!snap_depenetrate_vs_tris(mesh_b, &model_b,
                                        env_verts, env_count, &dep)) {
            converged_at = iter;
            break;
        }
        pos_x += dep.push.x * 1.001f;
        pos_y += dep.push.y * 1.001f;
        pos_z += dep.push.z * 1.001f;
    }

    printf("  iterative_convergence: converged at iter %d, "
           "final pos=(%.4f, %.4f, %.4f)\n",
           converged_at, pos_x, pos_y, pos_z);

    ASSERT(converged_at >= 0);
    ASSERT(converged_at < 10);
    /* Entity should have moved in +X direction to clear the overlap. */
    ASSERT(pos_x >= 0.99f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Null inputs should not crash. */
static int test_sat_null_safety(void) {
    mat4_t identity = mat4_identity();
    snap_depenetrate_result_t result;
    vec3_t dummy[3] = {{0,0,0},{1,0,0},{0,1,0}};

    ASSERT(!snap_depenetrate_vs_tris(NULL, &identity, dummy, 1, &result));
    ASSERT(!snap_depenetrate_vs_tris(NULL, NULL, NULL, 0, NULL));
    return 1;
}

/* ---- Main ---- */

int main(void) {
    int fails = 0, total = 0;

    RUN(test_plane_box_on_surface);
    RUN(test_plane_box_above_surface);
    RUN(test_plane_angled_normal);
    RUN(test_plane_scaled_box);
    RUN(test_plane_null_safety);
    RUN(test_sat_crossing_boxes);
    RUN(test_sat_separated);
    RUN(test_sat_floor_triangle);
    RUN(test_sat_edge_crossing);
    RUN(test_containment_env_inside_entity);
    RUN(test_capsule_on_box_edge);
    RUN(test_capsule_after_plane_push);
    RUN(test_box_push_direction);
    RUN(test_iterative_convergence);
    RUN(test_sat_null_safety);

    printf("\n%d / %d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
