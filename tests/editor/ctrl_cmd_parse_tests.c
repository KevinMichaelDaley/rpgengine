/**
 * @file ctrl_cmd_parse_tests.c
 * @brief Tests for TUI command text → JSON conversion and validation.
 *
 * Validates that ctrl_cmd_build_json() correctly:
 *   - Converts valid commands to proper JSON
 *   - Rejects unknown commands (returns 0)
 *   - Rejects invalid argument types (non-numeric where numeric expected)
 *   - Rejects invalid entity type names for spawn
 *   - Rejects insufficient required arguments
 */

#include "ferrum/editor/ctrl_cmd_defs.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ── Test harness ──────────────────────────────────────────────────── */

static int s_pass;
static int s_fail;

#define ASSERT_TRUE(cond, msg) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while (0)

#define ASSERT_EQ_U32(a, b, msg) do { \
    uint32_t a_ = (a), b_ = (b); \
    if (a_ == b_) { s_pass++; } \
    else { s_fail++; printf("  FAIL [%s:%d] %s (got %u, expected %u)\n", \
                            __FILE__, __LINE__, msg, a_, b_); } \
} while (0)

/* ── Helpers ───────────────────────────────────────────────────────── */

static char s_buf[2048];

/** Build JSON and return length. */
static uint32_t build(const char *input, uint32_t id) {
    memset(s_buf, 0, sizeof(s_buf));
    return ctrl_cmd_build_json(input, s_buf, sizeof(s_buf), id);
}

/** Check that the output contains a substring. */
static bool contains(const char *needle) {
    return strstr(s_buf, needle) != NULL;
}

/* ── Happy path tests ──────────────────────────────────────────────── */

static void test_spawn_box_with_pos(void) {
    printf("  spawn box 0 5 0...\n");
    uint32_t len = build("spawn box 0 5 0", 1);
    ASSERT_TRUE(len > 0, "spawn box should produce JSON");
    ASSERT_TRUE(contains("\"cmd\":\"spawn\""), "should have cmd=spawn");
    ASSERT_TRUE(contains("\"type\":\"box\""), "should have type=box");
}

static void test_spawn_sphere_named(void) {
    printf("  spawn sphere myball 1 2 3...\n");
    uint32_t len = build("spawn sphere myball 1 2 3", 2);
    ASSERT_TRUE(len > 0, "spawn sphere with name should produce JSON");
    ASSERT_TRUE(contains("\"type\":\"sphere\""), "should have type=sphere");
    ASSERT_TRUE(contains("\"name\":\"myball\""), "should have name=myball");
}

static void test_spawn_with_rot_and_scale(void) {
    printf("  spawn box 0 5 0 0 90 0 2 2 2...\n");
    uint32_t len = build("spawn box 0 5 0 0 90 0 2 2 2", 3);
    ASSERT_TRUE(len > 0, "spawn with rot+scale should produce JSON");
    ASSERT_TRUE(contains("\"rot\""), "should have rotation");
    ASSERT_TRUE(contains("\"scale\""), "should have scale");
}

static void test_delete_no_args(void) {
    printf("  delete...\n");
    uint32_t len = build("delete", 4);
    ASSERT_TRUE(len > 0, "delete should produce JSON");
    ASSERT_TRUE(contains("\"cmd\":\"delete\""), "should have cmd=delete");
}

static void test_move_valid(void) {
    printf("  move 1 2 3...\n");
    uint32_t len = build("move 1 2 3", 5);
    ASSERT_TRUE(len > 0, "move should produce JSON");
    ASSERT_TRUE(contains("\"cmd\":\"move\""), "should have cmd=move");
}

static void test_select_by_name(void) {
    printf("  select player...\n");
    uint32_t len = build("select player", 6);
    ASSERT_TRUE(len > 0, "select should produce JSON");
    ASSERT_TRUE(contains("\"entity_id\":\"player\""), "should have entity_id");
}

static void test_physics_pause(void) {
    printf("  physics_pause...\n");
    uint32_t len = build("physics_pause", 7);
    ASSERT_TRUE(len > 0, "physics_pause should produce JSON");
    ASSERT_TRUE(contains("\"cmd\":\"physics_pause\""), "should have cmd");
}

static void test_alias_works(void) {
    printf("  del (alias for delete)...\n");
    uint32_t len = build("del", 8);
    ASSERT_TRUE(len > 0, "alias 'del' should produce JSON");
    ASSERT_TRUE(contains("\"cmd\":\"delete\""), "should resolve to canonical name");
}

static void test_select_near_valid(void) {
    printf("  select_near 5.0...\n");
    uint32_t len = build("select_near 5.0", 9);
    ASSERT_TRUE(len > 0, "select_near should produce JSON");
    ASSERT_TRUE(contains("\"dist\""), "should have dist field");
}

static void test_joint_valid(void) {
    printf("  joint hinge a b 0 1 0...\n");
    uint32_t len = build("joint hinge a b 0 1 0", 10);
    ASSERT_TRUE(len > 0, "joint should produce JSON");
    ASSERT_TRUE(contains("\"joint_type\":\"hinge\""), "should have joint_type");
}

/* ── Unknown command tests ─────────────────────────────────────────── */

static void test_unknown_command_numeric(void) {
    printf("  100 (unknown)...\n");
    uint32_t len = build("100", 20);
    ASSERT_EQ_U32(len, 0, "numeric-only input should be rejected");
}

static void test_unknown_command_gibberish(void) {
    printf("  doddoby (unknown)...\n");
    uint32_t len = build("doddoby", 21);
    ASSERT_EQ_U32(len, 0, "unknown command should be rejected");
}

static void test_unknown_command_with_args(void) {
    printf("  foobar 1 2 3 (unknown)...\n");
    uint32_t len = build("foobar 1 2 3", 22);
    ASSERT_EQ_U32(len, 0, "unknown command with args should be rejected");
}

/* ── Invalid argument tests ────────────────────────────────────────── */

static void test_spawn_invalid_type(void) {
    printf("  spawn 10a big (bad type)...\n");
    uint32_t len = build("spawn 10a big", 30);
    ASSERT_EQ_U32(len, 0, "spawn with invalid type should be rejected");
}

static void test_spawn_missing_type(void) {
    printf("  spawn (no type)...\n");
    uint32_t len = build("spawn", 31);
    ASSERT_EQ_U32(len, 0, "spawn with no type should be rejected");
}

static void test_spawn_bad_pos_token(void) {
    printf("  spawn box abc 2 3 (non-numeric pos)...\n");
    uint32_t len = build("spawn box abc 2 3", 32);
    ASSERT_EQ_U32(len, 0, "spawn with non-numeric position should be rejected");
}

static void test_move_non_numeric(void) {
    printf("  move big small tall (non-numeric delta)...\n");
    uint32_t len = build("move big small tall", 33);
    ASSERT_EQ_U32(len, 0, "move with non-numeric args should be rejected");
}

static void test_move_partial_numeric(void) {
    printf("  move 1 abc 3 (one bad arg)...\n");
    uint32_t len = build("move 1 abc 3", 34);
    ASSERT_EQ_U32(len, 0, "move with one non-numeric arg should be rejected");
}

static void test_move_missing_args(void) {
    printf("  move (no args)...\n");
    uint32_t len = build("move", 35);
    ASSERT_EQ_U32(len, 0, "move with no args should be rejected");
}

static void test_rotate_missing_args(void) {
    printf("  rotate 1 (not enough)...\n");
    uint32_t len = build("rotate 1", 36);
    ASSERT_EQ_U32(len, 0, "rotate with too few args should be rejected");
}

static void test_bevel_non_numeric(void) {
    printf("  bevel abc (non-numeric amount)...\n");
    uint32_t len = build("bevel abc", 37);
    ASSERT_EQ_U32(len, 0, "bevel with non-numeric amount should be rejected");
}

static void test_spawn_bad_rot_token(void) {
    printf("  spawn box 0 5 0 abc 90 0 (non-numeric rot)...\n");
    uint32_t len = build("spawn box 0 5 0 abc 90 0", 38);
    ASSERT_EQ_U32(len, 0, "spawn with non-numeric rotation should be rejected");
}

static void test_select_near_non_numeric(void) {
    printf("  select_near abc (non-numeric dist)...\n");
    uint32_t len = build("select_near abc", 39);
    ASSERT_EQ_U32(len, 0, "select_near with non-numeric dist should be rejected");
}

static void test_physics_material_bad_float(void) {
    printf("  physics_material ground abc 0.1 (bad friction)...\n");
    uint32_t len = build("physics_material ground abc 0.1", 40);
    ASSERT_EQ_U32(len, 0, "physics_material with non-numeric friction should be rejected");
}

/* ── Edge cases ────────────────────────────────────────────────────── */

static void test_empty_input(void) {
    printf("  (empty)...\n");
    uint32_t len = build("", 50);
    ASSERT_EQ_U32(len, 0, "empty input should be rejected");
}

static void test_whitespace_only(void) {
    printf("  (whitespace only)...\n");
    uint32_t len = build("   ", 51);
    ASSERT_EQ_U32(len, 0, "whitespace-only input should be rejected");
}

static void test_help_query(void) {
    printf("  spawn ? (help query)...\n");
    uint32_t len = build("spawn ?", 52);
    ASSERT_EQ_U32(len, 0, "help query should return 0");
}

static void test_negative_numbers_valid(void) {
    printf("  move -1 -2.5 -3...\n");
    uint32_t len = build("move -1 -2.5 -3", 53);
    ASSERT_TRUE(len > 0, "negative numbers should be valid");
}

static void test_spawn_capsule_valid(void) {
    printf("  spawn capsule...\n");
    uint32_t len = build("spawn capsule 0 0 0", 54);
    ASSERT_TRUE(len > 0, "spawn capsule should be valid");
}

static void test_spawn_mesh_valid(void) {
    printf("  spawn mesh 0 0 0...\n");
    uint32_t len = build("spawn mesh 0 0 0", 55);
    ASSERT_TRUE(len > 0, "spawn mesh should be valid");
}

static void test_spawn_halfspace_valid(void) {
    printf("  spawn halfspace 0 0 0...\n");
    uint32_t len = build("spawn halfspace 0 0 0", 56);
    ASSERT_TRUE(len > 0, "spawn halfspace should be valid");
}

static void test_spawn_marker_valid(void) {
    printf("  spawn marker 0 0 0...\n");
    uint32_t len = build("spawn marker 0 0 0", 57);
    ASSERT_TRUE(len > 0, "spawn marker should be valid");
}

/* ── Main ──────────────────────────────────────────────────────────── */

int main(void) {
    printf("ctrl_cmd_parse_tests\n");
    printf("== happy path ==\n");
    test_spawn_box_with_pos();
    test_spawn_sphere_named();
    test_spawn_with_rot_and_scale();
    test_delete_no_args();
    test_move_valid();
    test_select_by_name();
    test_physics_pause();
    test_alias_works();
    test_select_near_valid();
    test_joint_valid();

    printf("== unknown commands ==\n");
    test_unknown_command_numeric();
    test_unknown_command_gibberish();
    test_unknown_command_with_args();

    printf("== invalid arguments ==\n");
    test_spawn_invalid_type();
    test_spawn_missing_type();
    test_spawn_bad_pos_token();
    test_move_non_numeric();
    test_move_partial_numeric();
    test_move_missing_args();
    test_rotate_missing_args();
    test_bevel_non_numeric();
    test_spawn_bad_rot_token();
    test_select_near_non_numeric();
    test_physics_material_bad_float();

    printf("== edge cases ==\n");
    test_empty_input();
    test_whitespace_only();
    test_help_query();
    test_negative_numbers_valid();
    test_spawn_capsule_valid();
    test_spawn_mesh_valid();
    test_spawn_halfspace_valid();
    test_spawn_marker_valid();

    printf("\n%d passed, %d failed\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
