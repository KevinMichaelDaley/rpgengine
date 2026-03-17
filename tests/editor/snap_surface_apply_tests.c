/**
 * @file snap_surface_apply_tests.c
 * @brief Tests for snap apply functions (face, vertex, surface offset).
 */

#include "ferrum/editor/viewport/snap/snap_surface_apply.h"
#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/editor/viewport/snap/snap_raycast.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

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

/* ---- Tests ---- */

/** Face snap sets entity position to hit position. */
static int test_apply_face_position(void) {
    edit_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.pos[0] = 10; ent.pos[1] = 10; ent.pos[2] = 10;
    ent.scale[0] = 1; ent.scale[1] = 1; ent.scale[2] = 1;
    ent.orientation = (quat_t){0, 0, 0, 1};

    snap_hit_t hit;
    memset(&hit, 0, sizeof(hit));
    hit.position = (vec3_t){3, 0, 5};
    hit.normal = (vec3_t){0, 1, 0};
    hit.valid = true;

    snap_apply_face(&ent, &hit);

    ASSERT_NEAR(ent.pos[0], 3.0f, 0.01f);
    ASSERT_NEAR(ent.pos[1], 0.0f, 0.01f);
    ASSERT_NEAR(ent.pos[2], 5.0f, 0.01f);
    return 1;
}

/** Face snap orients entity so local +Y aligns with face normal. */
static int test_apply_face_orientation_up(void) {
    edit_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.scale[0] = 1; ent.scale[1] = 1; ent.scale[2] = 1;
    ent.orientation = (quat_t){0, 0, 0, 1};

    snap_hit_t hit;
    memset(&hit, 0, sizeof(hit));
    hit.position = (vec3_t){0, 0, 0};
    hit.normal = (vec3_t){0, 1, 0};  /* +Y normal — identity orientation. */
    hit.valid = true;

    snap_apply_face(&ent, &hit);

    /* With +Y normal, the orientation should be close to identity. */
    vec3_t local_y = quat_rotate_vec3(ent.orientation, (vec3_t){0, 1, 0});
    ASSERT_NEAR(local_y.x, 0.0f, 0.1f);
    ASSERT_NEAR(local_y.y, 1.0f, 0.1f);
    ASSERT_NEAR(local_y.z, 0.0f, 0.1f);
    return 1;
}

/** Face snap with sideways normal reorients entity. */
static int test_apply_face_orientation_side(void) {
    edit_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.scale[0] = 1; ent.scale[1] = 1; ent.scale[2] = 1;
    ent.orientation = (quat_t){0, 0, 0, 1};

    snap_hit_t hit;
    memset(&hit, 0, sizeof(hit));
    hit.position = (vec3_t){0, 0, 0};
    hit.normal = (vec3_t){1, 0, 0};  /* +X normal. */
    hit.valid = true;

    snap_apply_face(&ent, &hit);

    /* Entity's local +Y should now point along +X world. */
    vec3_t local_y = quat_rotate_vec3(ent.orientation, (vec3_t){0, 1, 0});
    ASSERT_NEAR(local_y.x, 1.0f, 0.1f);
    ASSERT_NEAR(local_y.y, 0.0f, 0.1f);
    ASSERT_NEAR(local_y.z, 0.0f, 0.1f);
    return 1;
}

/** Vertex snap sets position to vertex world position. */
static int test_apply_vertex_position(void) {
    edit_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.pos[0] = 10; ent.pos[1] = 10; ent.pos[2] = 10;
    ent.scale[0] = 1; ent.scale[1] = 1; ent.scale[2] = 1;
    ent.orientation = (quat_t){0, 0, 0, 1};

    /* Snap mesh with known vertices. */
    float positions[9] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.5f, 0.0f, 1.0f
    };
    float normals[9] = {
        0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f
    };
    uint32_t indices[3] = {0, 1, 2};
    snap_mesh_t mesh = {
        .positions = positions,
        .normals = normals,
        .indices = indices,
        .vertex_count = 3,
        .index_count = 3
    };

    snap_hit_t hit;
    memset(&hit, 0, sizeof(hit));
    hit.position = (vec3_t){0.9f, 0.0f, 0.1f};  /* Close to vertex 1. */
    hit.normal = (vec3_t){0, 1, 0};
    hit.face_index = 0;
    hit.valid = true;

    mat4_t model = mat4_identity();

    snap_apply_vertex(&ent, &hit, &mesh, &model);

    /* Should snap to vertex 1: (1, 0, 0). */
    ASSERT_NEAR(ent.pos[0], 1.0f, 0.01f);
    ASSERT_NEAR(ent.pos[1], 0.0f, 0.01f);
    ASSERT_NEAR(ent.pos[2], 0.0f, 0.01f);
    return 1;
}

/** Vertex snap uses vertex normal for orientation. */
static int test_apply_vertex_orientation(void) {
    edit_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.scale[0] = 1; ent.scale[1] = 1; ent.scale[2] = 1;
    ent.orientation = (quat_t){0, 0, 0, 1};

    float positions[9] = {0,0,0, 1,0,0, 0.5f,0,1};
    float normals[9] = {
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,   /* Vertex 1 has +Z normal. */
        0.0f, 1.0f, 0.0f
    };
    uint32_t indices[3] = {0, 1, 2};
    snap_mesh_t mesh = {
        .positions = positions,
        .normals = normals,
        .indices = indices,
        .vertex_count = 3,
        .index_count = 3
    };

    snap_hit_t hit;
    memset(&hit, 0, sizeof(hit));
    hit.position = (vec3_t){0.9f, 0.0f, 0.0f};  /* Near vertex 1. */
    hit.normal = (vec3_t){0, 1, 0};
    hit.face_index = 0;
    hit.valid = true;

    mat4_t model = mat4_identity();
    snap_apply_vertex(&ent, &hit, &mesh, &model);

    /* Entity +Y should align with vertex 1's normal (+Z). */
    vec3_t local_y = quat_rotate_vec3(ent.orientation, (vec3_t){0, 1, 0});
    ASSERT_NEAR(local_y.z, 1.0f, 0.1f);
    return 1;
}

/** Surface snap offsets entity along normal by half-extent. */
static int test_apply_surface_offset(void) {
    edit_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.pos[0] = 10; ent.pos[1] = 10; ent.pos[2] = 10;
    ent.scale[0] = 2.0f;
    ent.scale[1] = 2.0f;
    ent.scale[2] = 2.0f;
    ent.orientation = (quat_t){0, 0, 0, 1};

    snap_hit_t hit;
    memset(&hit, 0, sizeof(hit));
    hit.position = (vec3_t){3, 0, 5};
    hit.normal = (vec3_t){0, 1, 0};
    hit.valid = true;

    /* Entity AABB half-extents: half a unit box scaled by 2 = 1.0 each axis.
     * Offset along +Y normal = 1.0. */
    vec3_t half_extents = {1.0f, 1.0f, 1.0f};

    snap_apply_on_surface(&ent, &hit, half_extents);

    ASSERT_NEAR(ent.pos[0], 3.0f, 0.01f);
    ASSERT_NEAR(ent.pos[1], 1.0f, 0.01f);  /* 0 + 1.0 offset. */
    ASSERT_NEAR(ent.pos[2], 5.0f, 0.01f);
    return 1;
}

/** Surface snap with angled normal projects half-extents correctly. */
static int test_apply_surface_offset_angled(void) {
    edit_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.scale[0] = 1; ent.scale[1] = 1; ent.scale[2] = 1;
    ent.orientation = (quat_t){0, 0, 0, 1};

    /* Normal at 45 degrees between +Y and +X. */
    float inv_sqrt2 = 1.0f / sqrtf(2.0f);
    snap_hit_t hit;
    memset(&hit, 0, sizeof(hit));
    hit.position = (vec3_t){0, 0, 0};
    hit.normal = (vec3_t){inv_sqrt2, inv_sqrt2, 0};
    hit.valid = true;

    /* Half-extents of 0.5 on each axis. */
    vec3_t half_extents = {0.5f, 0.5f, 0.5f};

    /* Offset = dot(half_extents, abs(normal)) = 0.5 * (inv_sqrt2 + inv_sqrt2 + 0) */
    float expected_offset = 0.5f * (inv_sqrt2 + inv_sqrt2);

    snap_apply_on_surface(&ent, &hit, half_extents);

    ASSERT_NEAR(ent.pos[0], inv_sqrt2 * expected_offset, 0.01f);
    ASSERT_NEAR(ent.pos[1], inv_sqrt2 * expected_offset, 0.01f);
    return 1;
}

/** Invalid hit produces no changes. */
static int test_apply_face_invalid_hit(void) {
    edit_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.pos[0] = 10; ent.pos[1] = 20; ent.pos[2] = 30;
    ent.scale[0] = 1; ent.scale[1] = 1; ent.scale[2] = 1;
    ent.orientation = (quat_t){0, 0, 0, 1};

    snap_hit_t hit;
    memset(&hit, 0, sizeof(hit));
    hit.valid = false;

    snap_apply_face(&ent, &hit);

    /* Position should be unchanged. */
    ASSERT_NEAR(ent.pos[0], 10.0f, 0.01f);
    ASSERT_NEAR(ent.pos[1], 20.0f, 0.01f);
    ASSERT_NEAR(ent.pos[2], 30.0f, 0.01f);
    return 1;
}

/* ---- Main ---- */

int main(void) {
    int fails = 0, total = 0;

    RUN(test_apply_face_position);
    RUN(test_apply_face_orientation_up);
    RUN(test_apply_face_orientation_side);
    RUN(test_apply_vertex_position);
    RUN(test_apply_vertex_orientation);
    RUN(test_apply_surface_offset);
    RUN(test_apply_surface_offset_angled);
    RUN(test_apply_face_invalid_hit);

    printf("\n%d / %d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
