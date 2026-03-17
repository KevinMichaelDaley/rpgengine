/**
 * @file snap_surface_cast_tests.c
 * @brief Tests for surface snap raycasting against scene entities.
 */

#include "ferrum/editor/viewport/snap/snap_surface_cast.h"
#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_matrix.h"
#include "ferrum/math/vec3.h"
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

/* ---- Test helpers ---- */

/** Create a unit quad (2 triangles) in the XZ plane at Y=0. */
static void make_quad_mesh(snap_mesh_cache_t *cache, uint32_t entity_id) {
    /* 4 vertices forming a quad from (-0.5, 0, -0.5) to (0.5, 0, 0.5). */
    float positions[12] = {
        -0.5f, 0.0f, -0.5f,
         0.5f, 0.0f, -0.5f,
         0.5f, 0.0f,  0.5f,
        -0.5f, 0.0f,  0.5f
    };
    float normals[12] = {
        0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f
    };
    uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    snap_mesh_cache_insert(cache, entity_id, positions, normals,
                            indices, 4, 6);
}

/** Set up a default entity at a given position. */
static void setup_entity(edit_entity_t *ent, float x, float y, float z,
                           uint32_t type) {
    memset(ent, 0, sizeof(*ent));
    ent->pos[0] = x;
    ent->pos[1] = y;
    ent->pos[2] = z;
    ent->scale[0] = 1.0f;
    ent->scale[1] = 1.0f;
    ent->scale[2] = 1.0f;
    ent->orientation = (quat_t){0, 0, 0, 1};
    ent->type = type;
    ent->active = true;
}

/* ---- Tests ---- */

/** Ray straight down hits a mesh entity's quad face. */
static int test_cast_face_hit(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    make_quad_mesh(&cache, 0);

    edit_entity_t entities[4];
    memset(entities, 0, sizeof(entities));
    setup_entity(&entities[0], 0, 0, 0, EDIT_ENTITY_TYPE_MESH);

    vec3_t origin = {0, 5, 0};
    vec3_t dir = {0, -1, 0};

    snap_hit_t hit;
    snap_surface_cast_ray(origin, dir, entities, 4, &cache, UINT32_MAX, &hit);

    ASSERT(hit.valid);
    ASSERT(hit.entity_id == 0);
    ASSERT_NEAR(hit.position.y, 0.0f, 0.01f);
    /* Normal should point up (Y+). */
    ASSERT(fabsf(hit.normal.y) > 0.9f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Ray misses all entities. */
static int test_cast_miss(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    make_quad_mesh(&cache, 0);

    edit_entity_t entities[4];
    memset(entities, 0, sizeof(entities));
    setup_entity(&entities[0], 0, 0, 0, EDIT_ENTITY_TYPE_MESH);

    /* Ray pointing away from the quad. */
    vec3_t origin = {0, 5, 0};
    vec3_t dir = {0, 1, 0};  /* Up, away from quad at Y=0. */

    snap_hit_t hit;
    snap_surface_cast_ray(origin, dir, entities, 4, &cache, UINT32_MAX, &hit);

    ASSERT(!hit.valid);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Self-exclusion: excluded entity is not hit. */
static int test_cast_exclude_self(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    make_quad_mesh(&cache, 0);

    edit_entity_t entities[4];
    memset(entities, 0, sizeof(entities));
    setup_entity(&entities[0], 0, 0, 0, EDIT_ENTITY_TYPE_MESH);

    vec3_t origin = {0, 5, 0};
    vec3_t dir = {0, -1, 0};

    snap_hit_t hit;
    /* Exclude entity 0 — should miss. */
    snap_surface_cast_ray(origin, dir, entities, 4, &cache, 0, &hit);

    ASSERT(!hit.valid);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Nearest of two entities wins. */
static int test_cast_nearest_wins(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    make_quad_mesh(&cache, 0);
    make_quad_mesh(&cache, 1);

    edit_entity_t entities[4];
    memset(entities, 0, sizeof(entities));
    /* Entity 0 at Y=0. */
    setup_entity(&entities[0], 0, 0, 0, EDIT_ENTITY_TYPE_MESH);
    /* Entity 1 at Y=2 (closer to ray origin at Y=5). */
    setup_entity(&entities[1], 0, 2, 0, EDIT_ENTITY_TYPE_MESH);

    vec3_t origin = {0, 5, 0};
    vec3_t dir = {0, -1, 0};

    snap_hit_t hit;
    snap_surface_cast_ray(origin, dir, entities, 4, &cache, UINT32_MAX, &hit);

    ASSERT(hit.valid);
    ASSERT(hit.entity_id == 1);
    ASSERT_NEAR(hit.position.y, 2.0f, 0.01f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Hidden entities are skipped. */
static int test_cast_hidden_skipped(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    make_quad_mesh(&cache, 0);

    edit_entity_t entities[4];
    memset(entities, 0, sizeof(entities));
    setup_entity(&entities[0], 0, 0, 0, EDIT_ENTITY_TYPE_MESH);
    entities[0].hidden = true;

    vec3_t origin = {0, 5, 0};
    vec3_t dir = {0, -1, 0};

    snap_hit_t hit;
    snap_surface_cast_ray(origin, dir, entities, 4, &cache, UINT32_MAX, &hit);

    ASSERT(!hit.valid);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Entity with BOX type uses cached box snap mesh. */
static int test_cast_box_entity(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    /* Generate a box snap mesh for entity 0. */
    snap_mesh_retain_box(&cache, 0);

    edit_entity_t entities[4];
    memset(entities, 0, sizeof(entities));
    setup_entity(&entities[0], 0, 0, 0, EDIT_ENTITY_TYPE_BOX);

    /* Ray from above should hit top face at Y=0.5. */
    vec3_t origin = {0, 5, 0};
    vec3_t dir = {0, -1, 0};

    snap_hit_t hit;
    snap_surface_cast_ray(origin, dir, entities, 4, &cache, UINT32_MAX, &hit);

    ASSERT(hit.valid);
    ASSERT(hit.entity_id == 0);
    ASSERT_NEAR(hit.position.y, 0.5f, 0.01f);
    /* Normal should point up. */
    ASSERT(hit.normal.y > 0.9f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** Entity with scale transforms the mesh. */
static int test_cast_scaled_entity(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);
    snap_mesh_retain_box(&cache, 0);

    edit_entity_t entities[4];
    memset(entities, 0, sizeof(entities));
    setup_entity(&entities[0], 0, 0, 0, EDIT_ENTITY_TYPE_BOX);
    /* Scale by 2 → half-extent becomes 1.0 → top face at Y=1.0. */
    entities[0].scale[1] = 2.0f;

    vec3_t origin = {0, 5, 0};
    vec3_t dir = {0, -1, 0};

    snap_hit_t hit;
    snap_surface_cast_ray(origin, dir, entities, 4, &cache, UINT32_MAX, &hit);

    ASSERT(hit.valid);
    ASSERT_NEAR(hit.position.y, 1.0f, 0.01f);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/** NULL entities array produces no hit. */
static int test_cast_null_entities(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 4);

    vec3_t origin = {0, 5, 0};
    vec3_t dir = {0, -1, 0};

    snap_hit_t hit;
    snap_surface_cast_ray(origin, dir, NULL, 0, &cache, UINT32_MAX, &hit);

    ASSERT(!hit.valid);

    snap_mesh_cache_destroy(&cache);
    return 1;
}

/* ---- Main ---- */

int main(void) {
    int fails = 0, total = 0;

    RUN(test_cast_face_hit);
    RUN(test_cast_miss);
    RUN(test_cast_exclude_self);
    RUN(test_cast_nearest_wins);
    RUN(test_cast_hidden_skipped);
    RUN(test_cast_box_entity);
    RUN(test_cast_scaled_entity);
    RUN(test_cast_null_entities);

    printf("\n%d / %d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
