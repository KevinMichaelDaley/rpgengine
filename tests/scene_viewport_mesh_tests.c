/**
 * @file scene_viewport_mesh_tests.c
 * @brief Tests for viewport entity mesh cache (headless).
 *
 * Verifies non-GL aspects of the entity mesh cache: NULL handling,
 * sentinel initialization, and API contracts. Actual FVMA loading
 * requires a live GL context and is covered by integration tests.
 */

#include <stdio.h>
#include <string.h>

#include "ferrum/editor/scene/scene_viewport_render.h"

static int s_pass = 0;
static int s_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        s_fail++; \
    } else { \
        s_pass++; \
    } \
} while (0)

/* ---- Tests ---- */

static void test_load_null_state(void) {
    printf("test_load_null_state\n");
    uint8_t dummy[4] = {0};
    ASSERT(viewport_render_load_entity_mesh(NULL, 0, dummy, 4) == false,
           "load(NULL state) should return false");
}

static void test_load_null_data(void) {
    printf("test_load_null_data\n");
    viewport_render_state_t state;
    memset(&state, 0, sizeof(state));
    ASSERT(viewport_render_load_entity_mesh(&state, 0, NULL, 0) == false,
           "load(NULL data) should return false");
}

static void test_load_uninitialized_state(void) {
    printf("test_load_uninitialized_state\n");
    viewport_render_state_t state;
    memset(&state, 0, sizeof(state));
    /* state.initialized is false */
    uint8_t dummy[4] = {0};
    ASSERT(viewport_render_load_entity_mesh(&state, 0, dummy, 4) == false,
           "load(uninitialized) should return false");
}

static void test_unload_null_state(void) {
    printf("test_unload_null_state\n");
    /* Should not crash. */
    viewport_render_unload_entity_mesh(NULL, 0);
    ASSERT(true, "unload(NULL) should not crash");
}

static void test_unload_uninitialized_state(void) {
    printf("test_unload_uninitialized_state\n");
    viewport_render_state_t state;
    memset(&state, 0, sizeof(state));
    viewport_render_unload_entity_mesh(&state, 0);
    ASSERT(true, "unload(uninitialized) should not crash");
}

static void test_get_entity_mesh_null(void) {
    printf("test_get_entity_mesh_null\n");
    ASSERT(viewport_render_get_entity_mesh(NULL, 0) == NULL,
           "get(NULL) should return NULL");
}

static void test_get_entity_mesh_uninitialized(void) {
    printf("test_get_entity_mesh_uninitialized\n");
    viewport_render_state_t state;
    memset(&state, 0, sizeof(state));
    ASSERT(viewport_render_get_entity_mesh(&state, 0) == NULL,
           "get(uninitialized) should return NULL");
}

static void test_get_entity_mesh_no_cache(void) {
    printf("test_get_entity_mesh_no_cache\n");
    viewport_render_state_t state;
    memset(&state, 0, sizeof(state));
    state.initialized = true;
    /* entity_mesh_cache is NULL */
    ASSERT(viewport_render_get_entity_mesh(&state, 0) == NULL,
           "get(no cache) should return NULL");
}

static void test_get_entity_mesh_out_of_range(void) {
    printf("test_get_entity_mesh_out_of_range\n");
    viewport_render_state_t state;
    memset(&state, 0, sizeof(state));
    state.initialized = true;
    /* Simulate a small cache. */
    mesh_handle_t cache[2];
    memset(cache, 0xFF, sizeof(cache)); /* sentinel values */
    state.entity_mesh_cache = cache;
    state.entity_mesh_cache_cap = 2;
    ASSERT(viewport_render_get_entity_mesh(&state, 100) == NULL,
           "get(out of range) should return NULL");
}

static void test_load_entity_id_out_of_range(void) {
    printf("test_load_entity_id_out_of_range\n");
    viewport_render_state_t state;
    memset(&state, 0, sizeof(state));
    state.initialized = true;
    mesh_handle_t cache[2];
    memset(cache, 0xFF, sizeof(cache));
    state.entity_mesh_cache = cache;
    state.entity_mesh_cache_cap = 2;
    uint8_t dummy[4] = {0};
    ASSERT(viewport_render_load_entity_mesh(&state, 100, dummy, 4) == false,
           "load(entity_id >= cap) should return false");
}

int main(void) {
    printf("=== scene_viewport_mesh_tests ===\n");

    test_load_null_state();
    test_load_null_data();
    test_load_uninitialized_state();
    test_unload_null_state();
    test_unload_uninitialized_state();
    test_get_entity_mesh_null();
    test_get_entity_mesh_uninitialized();
    test_get_entity_mesh_no_cache();
    test_get_entity_mesh_out_of_range();
    test_load_entity_id_out_of_range();

    printf("\n%d passed, %d failed\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
