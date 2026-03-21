/**
 * @file asset_ref_widget_tests.c
 * @brief Tests for asset reference selector widget state management.
 */

#include "ferrum/editor/panels/asset_ref_widget.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Init tests ---- */

static void test_init_zeroes_state(void) {
    asset_ref_state_t state;
    /* Fill with garbage first. */
    memset(&state, 0xAB, sizeof(state));
    asset_ref_init(&state, 7); /* EDIT_ASSET_SKELETON */

    ASSERT(state.path[0] == '\0');
    ASSERT(state.display[0] == '\0');
    ASSERT(state.focused == false);
    ASSERT(state.confirmed == false);
    ASSERT(state.filter_type == 7);
}

static void test_init_null_safe(void) {
    /* Should not crash. */
    asset_ref_init(NULL, 0);
    ASSERT(1); /* Reached here = no crash. */
}

/* ---- set_path tests ---- */

static void test_set_path_copies(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 0);
    asset_ref_set_path(&state, "meshes/barrel.fvma");

    ASSERT(strcmp(state.path, "meshes/barrel.fvma") == 0);
    /* Display should be the filename portion. */
    ASSERT(strcmp(state.display, "barrel.fvma") == 0);
}

static void test_set_path_no_slash(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 0);
    asset_ref_set_path(&state, "humanoid.fskel");

    ASSERT(strcmp(state.path, "humanoid.fskel") == 0);
    ASSERT(strcmp(state.display, "humanoid.fskel") == 0);
}

static void test_set_path_null_clears(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 0);
    asset_ref_set_path(&state, "test.fvma");
    asset_ref_set_path(&state, NULL);

    ASSERT(state.path[0] == '\0');
    ASSERT(state.display[0] == '\0');
}

static void test_set_path_overflow_truncates(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 0);

    /* Build a path longer than 256 chars. */
    char long_path[512];
    memset(long_path, 'a', sizeof(long_path));
    long_path[511] = '\0';

    asset_ref_set_path(&state, long_path);
    ASSERT(strlen(state.path) < 256);
    ASSERT(state.path[255] == '\0');
}

static void test_set_path_null_state_safe(void) {
    asset_ref_set_path(NULL, "test");
    ASSERT(1);
}

/* ---- accept tests ---- */

static void test_accept_fills_path(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 7);

    asset_ref_accept(&state, "skeletons/goblin.fskel");
    ASSERT(strcmp(state.path, "skeletons/goblin.fskel") == 0);
    ASSERT(strcmp(state.display, "goblin.fskel") == 0);
}

static void test_accept_clears_confirmed(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 0);
    asset_ref_confirm(&state);
    ASSERT(state.confirmed == true);

    /* Accept resets confirmed so user can re-confirm the new path. */
    asset_ref_accept(&state, "new.fvma");
    ASSERT(state.confirmed == false);
}

static void test_accept_null_path_safe(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 0);
    asset_ref_accept(&state, NULL);
    ASSERT(state.path[0] == '\0');
}

static void test_accept_null_state_safe(void) {
    asset_ref_accept(NULL, "test");
    ASSERT(1);
}

/* ---- confirm tests ---- */

static void test_confirm_sets_flag(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 0);
    asset_ref_set_path(&state, "test.fvma");
    asset_ref_confirm(&state);

    ASSERT(state.confirmed == true);
}

static void test_confirm_with_empty_path(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 0);
    /* Confirm with no path set — should still set flag (caller decides if valid). */
    asset_ref_confirm(&state);
    ASSERT(state.confirmed == true);
}

static void test_confirm_null_safe(void) {
    asset_ref_confirm(NULL);
    ASSERT(1);
}

/* ---- filter_type tests ---- */

static void test_filter_type_stored(void) {
    asset_ref_state_t state;

    asset_ref_init(&state, 1); /* EDIT_ASSET_MESH */
    ASSERT(state.filter_type == 1);

    asset_ref_init(&state, 7); /* EDIT_ASSET_SKELETON */
    ASSERT(state.filter_type == 7);

    asset_ref_init(&state, 0); /* EDIT_ASSET_ANY */
    ASSERT(state.filter_type == 0);
}

/* ---- display extraction ---- */

static void test_display_extracts_deep_filename(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 0);
    asset_ref_set_path(&state, "assets/meshes/characters/humanoid.fvma");

    ASSERT(strcmp(state.display, "humanoid.fvma") == 0);
}

static void test_display_truncates_long_filename(void) {
    asset_ref_state_t state;
    asset_ref_init(&state, 0);

    /* Build filename longer than 64 chars. */
    char long_name[128];
    memset(long_name, 'x', 127);
    long_name[127] = '\0';

    asset_ref_set_path(&state, long_name);
    ASSERT(strlen(state.display) < 64);
}

int main(void) {
    printf("asset_ref_widget_tests:\n");
    test_init_zeroes_state();
    test_init_null_safe();
    test_set_path_copies();
    test_set_path_no_slash();
    test_set_path_null_clears();
    test_set_path_overflow_truncates();
    test_set_path_null_state_safe();
    test_accept_fills_path();
    test_accept_clears_confirmed();
    test_accept_null_path_safe();
    test_accept_null_state_safe();
    test_confirm_sets_flag();
    test_confirm_with_empty_path();
    test_confirm_null_safe();
    test_filter_type_stored();
    test_display_extracts_deep_filename();
    test_display_truncates_long_filename();
    printf("asset_ref_widget_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
