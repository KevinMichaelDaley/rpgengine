/**
 * @file per_object_gizmo_tests.c
 * @brief Tests for per-object gizmo mode: hit testing, drag application,
 *        and command sending for individual entity gizmos.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ferrum/editor/scene/scene_gizmo_per_object.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/editor/viewport/transform_basis.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"

static int tests_run = 0;
static int tests_passed = 0;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    tests_run++; \
    if (fn()) { tests_passed++; printf("OK   %s\n", #fn); } \
    else { printf("FAIL %s\n", #fn); } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); return 0; } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    if (fabsf((a)-(b)) > (eps)) { \
        printf("  ASSERT_NEAR FAILED: %g != %g (line %d)\n", \
               (double)(a), (double)(b), __LINE__); return 0; } \
} while(0)

/* ---- per_object_gizmo_build tests ---- */

/** Build per-object gizmos for 3 selected entities. */
static int test_build_gizmos_for_selection(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 64);
    edit_selection_t sel;
    edit_selection_init(&sel);

    /* Create 3 entities at different positions. */
    uint32_t e0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    uint32_t e1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    uint32_t e2 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *ent0 = edit_entity_store_get_mut(&store, e0);
    edit_entity_t *ent1 = edit_entity_store_get_mut(&store, e1);
    edit_entity_t *ent2 = edit_entity_store_get_mut(&store, e2);
    ent0->pos[0] = 1; ent0->pos[1] = 0; ent0->pos[2] = 0;
    ent1->pos[0] = 0; ent1->pos[1] = 2; ent1->pos[2] = 0;
    ent2->pos[0] = 0; ent2->pos[1] = 0; ent2->pos[2] = 3;
    ent0->orientation = (quat_t){0, 0, 0, 1};
    ent1->orientation = (quat_t){0, 0, 0, 1};
    ent2->orientation = (quat_t){0, 0, 0, 1};

    edit_selection_add(&sel, e0);
    edit_selection_add(&sel, e1);
    edit_selection_add(&sel, e2);

    per_object_gizmo_t gizmos[8];
    uint32_t count = per_object_gizmo_build(
        &store, &sel, GIZMO_MODE_TRANSLATE, TRANSFORM_BASIS_WORLD,
        NULL, NULL, gizmos, 8);

    ASSERT(count == 3);

    /* Each gizmo should be positioned at its entity's pos. */
    /* Order depends on entity iteration; just check all 3 positions exist. */
    bool found[3] = {false, false, false};
    for (uint32_t i = 0; i < count; i++) {
        if (fabsf(gizmos[i].gizmo.position.x - 1.0f) < 0.01f) found[0] = true;
        if (fabsf(gizmos[i].gizmo.position.y - 2.0f) < 0.01f) found[1] = true;
        if (fabsf(gizmos[i].gizmo.position.z - 3.0f) < 0.01f) found[2] = true;
    }
    ASSERT(found[0] && found[1] && found[2]);

    edit_entity_store_destroy(&store);
    edit_selection_destroy(&sel);
    return 1;
}

/** Build with empty selection returns 0. */
static int test_build_empty_selection(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);
    edit_selection_t sel;
    edit_selection_init(&sel);

    per_object_gizmo_t gizmos[4];
    uint32_t count = per_object_gizmo_build(
        &store, &sel, GIZMO_MODE_TRANSLATE, TRANSFORM_BASIS_WORLD,
        NULL, NULL, gizmos, 4);

    ASSERT(count == 0);

    edit_entity_store_destroy(&store);
    edit_selection_destroy(&sel);
    return 1;
}

/** Build with capacity smaller than selection truncates. */
static int test_build_truncates_to_capacity(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 64);
    edit_selection_t sel;
    edit_selection_init(&sel);

    for (int i = 0; i < 5; i++) {
        uint32_t eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
        edit_selection_add(&sel, eid);
    }

    per_object_gizmo_t gizmos[2];
    uint32_t count = per_object_gizmo_build(
        &store, &sel, GIZMO_MODE_TRANSLATE, TRANSFORM_BASIS_WORLD,
        NULL, NULL, gizmos, 2);

    ASSERT(count == 2);

    edit_entity_store_destroy(&store);
    edit_selection_destroy(&sel);
    return 1;
}

/** Build with local basis uses each entity's own orientation. */
static int test_build_local_basis(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);
    edit_selection_t sel;
    edit_selection_init(&sel);

    uint32_t e0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *ent0 = edit_entity_store_get_mut(&store, e0);
    ent0->pos[0] = 0; ent0->pos[1] = 0; ent0->pos[2] = 0;
    /* 90-degree Y rotation. */
    float half = 3.14159265358979323846f / 4.0f;
    ent0->orientation = (quat_t){0, sinf(half), 0, cosf(half)};

    edit_selection_add(&sel, e0);

    per_object_gizmo_t gizmos[4];
    uint32_t count = per_object_gizmo_build(
        &store, &sel, GIZMO_MODE_TRANSLATE, TRANSFORM_BASIS_LOCAL,
        NULL, NULL, gizmos, 4);

    ASSERT(count == 1);
    ASSERT(gizmos[0].entity_id == e0);

    /* The orientation matrix should NOT be identity (it's rotated). */
    /* Check that the X column of the orientation matrix has a Z component,
     * indicating the 90-degree Y rotation was applied. */
    float gx = gizmos[0].gizmo.orientation.m[0]; /* X-axis X component. */
    ASSERT(fabsf(gx) < 0.1f); /* Should be ~0 after 90-deg Y rotation. */

    edit_entity_store_destroy(&store);
    edit_selection_destroy(&sel);
    return 1;
}

/* ---- per_object_gizmo_pick tests ---- */

/** Pick returns INVALID_ID when no gizmo has a hit. */
static int test_pick_no_hit(void) {
    per_object_gizmo_t gizmos[1];
    gizmos[0].entity_id = 42;
    gizmo_state_init(&gizmos[0].gizmo);
    gizmos[0].gizmo.mode = GIZMO_MODE_TRANSLATE;
    gizmos[0].gizmo.position = (vec3_t){100, 100, 100};

    /* Ray pointing away from the gizmo. */
    editor_ray_t ray;
    ray.origin = (vec3_t){0, 0, 0};
    ray.direction = (vec3_t){0, 0, -1};

    mat4_t vp = mat4_identity();

    gizmo_axis_t hit_axis;
    uint32_t picked = per_object_gizmo_pick(
        gizmos, 1, &ray, 1.0f, &vp, 0.5f, 0.5f, &hit_axis);

    ASSERT(picked == EDIT_ENTITY_INVALID_ID);
    ASSERT(hit_axis == GIZMO_AXIS_NONE);

    return 1;
}

/* ---- per_object_gizmo_apply_drag tests ---- */

/** Apply translate drag moves only the target entity. */
static int test_apply_drag_single_entity(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);
    edit_selection_t sel;
    edit_selection_init(&sel);

    uint32_t e0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    uint32_t e1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *ent0 = edit_entity_store_get_mut(&store, e0);
    edit_entity_t *ent1 = edit_entity_store_get_mut(&store, e1);
    ent0->pos[0] = 0; ent0->pos[1] = 0; ent0->pos[2] = 0;
    ent1->pos[0] = 5; ent1->pos[1] = 5; ent1->pos[2] = 5;
    ent0->scale[0] = 1; ent0->scale[1] = 1; ent0->scale[2] = 1;
    ent1->scale[0] = 1; ent1->scale[1] = 1; ent1->scale[2] = 1;

    edit_selection_add(&sel, e0);
    edit_selection_add(&sel, e1);

    vec3_t delta = {1.0f, 2.0f, 3.0f};
    per_object_gizmo_apply_drag(&store, e0, GIZMO_MODE_TRANSLATE, delta);

    /* e0 should have moved. */
    ASSERT_NEAR(ent0->pos[0], 1.0f, 1e-5f);
    ASSERT_NEAR(ent0->pos[1], 2.0f, 1e-5f);
    ASSERT_NEAR(ent0->pos[2], 3.0f, 1e-5f);

    /* e1 should be unchanged. */
    ASSERT_NEAR(ent1->pos[0], 5.0f, 1e-5f);
    ASSERT_NEAR(ent1->pos[1], 5.0f, 1e-5f);
    ASSERT_NEAR(ent1->pos[2], 5.0f, 1e-5f);

    edit_entity_store_destroy(&store);
    edit_selection_destroy(&sel);
    return 1;
}

/** Apply scale drag modifies only the target entity. */
static int test_apply_drag_scale(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    uint32_t e0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *ent0 = edit_entity_store_get_mut(&store, e0);
    ent0->scale[0] = 1; ent0->scale[1] = 1; ent0->scale[2] = 1;

    vec3_t delta = {0.5f, 0.0f, 0.0f};
    per_object_gizmo_apply_drag(&store, e0, GIZMO_MODE_SCALE, delta);

    ASSERT_NEAR(ent0->scale[0], 1.5f, 1e-5f);
    ASSERT_NEAR(ent0->scale[1], 1.0f, 1e-5f);
    ASSERT_NEAR(ent0->scale[2], 1.0f, 1e-5f);

    edit_entity_store_destroy(&store);
    return 1;
}

/** Apply drag with invalid entity ID does nothing (no crash). */
static int test_apply_drag_invalid_entity(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    vec3_t delta = {1.0f, 0.0f, 0.0f};
    per_object_gizmo_apply_drag(&store, 999, GIZMO_MODE_TRANSLATE, delta);
    /* Should not crash. */

    edit_entity_store_destroy(&store);
    return 1;
}

/* ---- per_object_gizmo_apply_rotate tests ---- */

/** Rotate only applies to the target entity. */
static int test_apply_rotate_single_entity(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 16);

    uint32_t e0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    uint32_t e1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *ent0 = edit_entity_store_get_mut(&store, e0);
    edit_entity_t *ent1 = edit_entity_store_get_mut(&store, e1);
    ent0->orientation = (quat_t){0, 0, 0, 1};
    ent1->orientation = (quat_t){0, 0, 0, 1};

    /* 90-degree Y rotation. */
    float half = 3.14159265358979323846f / 4.0f;
    quat_t dq = {0, sinf(half), 0, cosf(half)};
    per_object_gizmo_apply_rotate(&store, e0, dq, NULL);

    /* e0 should be rotated. */
    ASSERT(fabsf(ent0->orientation.y - sinf(half)) < 0.01f);

    /* e1 should be unchanged. */
    ASSERT_NEAR(ent1->orientation.x, 0.0f, 1e-5f);
    ASSERT_NEAR(ent1->orientation.y, 0.0f, 1e-5f);
    ASSERT_NEAR(ent1->orientation.z, 0.0f, 1e-5f);
    ASSERT_NEAR(ent1->orientation.w, 1.0f, 1e-5f);

    edit_entity_store_destroy(&store);
    return 1;
}

int main(void) {
    printf("=== per_object_gizmo tests ===\n");

    /* Build tests. */
    RUN(test_build_gizmos_for_selection);
    RUN(test_build_empty_selection);
    RUN(test_build_truncates_to_capacity);
    RUN(test_build_local_basis);

    /* Pick tests. */
    RUN(test_pick_no_hit);

    /* Drag tests. */
    RUN(test_apply_drag_single_entity);
    RUN(test_apply_drag_scale);
    RUN(test_apply_drag_invalid_entity);

    /* Rotate tests. */
    RUN(test_apply_rotate_single_entity);

    printf("\n%d passed, %d failed\n", tests_passed, tests_run - tests_passed);
    return (tests_passed == tests_run) ? 0 : 1;
}
