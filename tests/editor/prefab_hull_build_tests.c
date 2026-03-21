/**
 * @file prefab_hull_build_tests.c
 * @brief Tests for prefab hull building from marker entities.
 *
 * Validates that prefab_hull_build_from_markers scans entities for
 * markers parented to a specific bone and builds a convex hull from
 * their positions.
 */

#include "ferrum/editor/scene/prefab/prefab_hull_build.h"
#include "ferrum/editor/scene/prefab/prefab_bone_parent.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Helper: spawn a marker at position ---- */

static uint32_t spawn_marker(edit_entity_store_t *store, uint32_t root_id,
                             uint32_t bone_index, float x, float y, float z) {
    uint32_t id = edit_entity_store_create(store, EDIT_ENTITY_TYPE_MARKER);
    if (id == EDIT_ENTITY_INVALID_ID) return id;

    edit_entity_t *ent = edit_entity_store_get_mut(store, id);
    ent->pos[0] = x;
    ent->pos[1] = y;
    ent->pos[2] = z;

    prefab_parent_to_bone(store, id, root_id, bone_index);
    return id;
}

/* ---- Tests ---- */

/** Fewer than 4 markers returns invalid hull. */
static void test_fewer_than_4(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    spawn_marker(&store, root, 0, 0, 0, 0);
    spawn_marker(&store, root, 0, 1, 0, 0);
    spawn_marker(&store, root, 0, 0, 1, 0);
    /* Only 3 markers. */

    prefab_hull_result_t result;
    bool ok = prefab_hull_build_from_markers(&store, root, 0, &result);
    ASSERT(!ok);
    ASSERT(!result.valid);
    ASSERT(result.marker_count == 3);

    edit_entity_store_destroy(&store);
}

/** No markers at all. */
static void test_no_markers(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);

    prefab_hull_result_t result;
    bool ok = prefab_hull_build_from_markers(&store, root, 0, &result);
    ASSERT(!ok);
    ASSERT(!result.valid);
    ASSERT(result.marker_count == 0);

    edit_entity_store_destroy(&store);
}

/** Exactly 4 markers forming a tetrahedron. */
static void test_exactly_4(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    spawn_marker(&store, root, 0, 0, 0, 0);
    spawn_marker(&store, root, 0, 1, 0, 0);
    spawn_marker(&store, root, 0, 0, 1, 0);
    spawn_marker(&store, root, 0, 0, 0, 1);

    prefab_hull_result_t result;
    bool ok = prefab_hull_build_from_markers(&store, root, 0, &result);
    ASSERT(ok);
    ASSERT(result.valid);
    ASSERT(result.marker_count == 4);
    ASSERT(result.hull.vertex_count >= 4);

    edit_entity_store_destroy(&store);
}

/** 8 markers forming a cube. */
static void test_8_markers_cube(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    /* Cube corners. */
    spawn_marker(&store, root, 2, -1, -1, -1);
    spawn_marker(&store, root, 2,  1, -1, -1);
    spawn_marker(&store, root, 2, -1,  1, -1);
    spawn_marker(&store, root, 2,  1,  1, -1);
    spawn_marker(&store, root, 2, -1, -1,  1);
    spawn_marker(&store, root, 2,  1, -1,  1);
    spawn_marker(&store, root, 2, -1,  1,  1);
    spawn_marker(&store, root, 2,  1,  1,  1);

    prefab_hull_result_t result;
    bool ok = prefab_hull_build_from_markers(&store, root, 2, &result);
    ASSERT(ok);
    ASSERT(result.valid);
    ASSERT(result.marker_count == 8);
    ASSERT(result.hull.vertex_count == 8);
    ASSERT(result.hull.face_count == 6);

    edit_entity_store_destroy(&store);
}

/** Marker count function returns correct count. */
static void test_count_markers(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);
    spawn_marker(&store, root, 0, 0, 0, 0);
    spawn_marker(&store, root, 0, 1, 0, 0);
    /* Marker on a different bone — should not count. */
    spawn_marker(&store, root, 1, 0, 0, 0);

    ASSERT(prefab_hull_count_markers(&store, root, 0) == 2);
    ASSERT(prefab_hull_count_markers(&store, root, 1) == 1);
    ASSERT(prefab_hull_count_markers(&store, root, 5) == 0);

    edit_entity_store_destroy(&store);
}

/** Non-marker entities parented to bone are ignored. */
static void test_non_markers_ignored(void) {
    edit_entity_store_t store;
    edit_entity_store_init(&store, 32);

    uint32_t root = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_MESH);

    /* Parent a box collider to bone 0 — not a marker. */
    uint32_t box = edit_entity_store_create(&store,
                                             EDIT_ENTITY_TYPE_COLLIDER_BOX);
    prefab_parent_to_bone(&store, box, root, 0);

    /* Add 4 markers. */
    spawn_marker(&store, root, 0, 0, 0, 0);
    spawn_marker(&store, root, 0, 1, 0, 0);
    spawn_marker(&store, root, 0, 0, 1, 0);
    spawn_marker(&store, root, 0, 0, 0, 1);

    /* Marker count should be 4 (not 5). */
    ASSERT(prefab_hull_count_markers(&store, root, 0) == 4);

    prefab_hull_result_t result;
    bool ok = prefab_hull_build_from_markers(&store, root, 0, &result);
    ASSERT(ok);
    ASSERT(result.marker_count == 4);

    edit_entity_store_destroy(&store);
}

/** NULL args return false/0. */
static void test_null_args(void) {
    prefab_hull_result_t result;
    ASSERT(!prefab_hull_build_from_markers(NULL, 0, 0, &result));
    ASSERT(prefab_hull_count_markers(NULL, 0, 0) == 0);
}

/* ---- Main ---- */

int main(void) {
    printf("prefab_hull_build_tests:\n");

    test_fewer_than_4();
    test_no_markers();
    test_exactly_4();
    test_8_markers_cube();
    test_count_markers();
    test_non_markers_ignored();
    test_null_args();

    printf("prefab_hull_build_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
