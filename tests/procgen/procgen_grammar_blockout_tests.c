/**
 * @file procgen_grammar_blockout_tests.c
 * @brief Tests for the blockout grammar rasterizer.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/procgen/procgen_types.h"
#include "ferrum/procgen/procgen_layout.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/grammar_blockout.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn)                                                              \
    do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n", #fn); } while (0)
#define ASSERT_TRUE(expr)                                                    \
    do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_EQ(a, b)          ASSERT_TRUE((a) == (b))
#define ASSERT_INT_EQ(a, b)      ASSERT_TRUE((int)(a) == (int)(b))
#define ASSERT_FLOAT_NEAR(a, b, eps) ASSERT_TRUE(fabsf((float)(a) - (float)(b)) <= (eps))
#define ASSERT_STREQ(a, b)       ASSERT_TRUE(strcmp((a), (b)) == 0)
#define PASS()                   g_pass++

#define TOK_BUF_CAP 256

static void free_layout(fr_dungeon_layout_t *layout) {
    free(layout->rooms);
    free(layout->corridors);
    free(layout->openings);
    free(layout->ramps);
    free(layout->markers);
    memset(layout, 0, sizeof(*layout));
}

/* ── Helpers ───────────────────────────────────────────────────── */

static int tokenize_and_rasterize(const char *input, fr_dungeon_layout_t *layout) {
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    tok_error_t rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    if (rc != TOK_ERR_NONE) {
        printf("tokenize error: %s\n", err);
        return -1;
    }
    rc = grammar_blockout_rasterize(tokens, count, layout, err, sizeof(err));
    if (rc != 0) {
        printf("rasterize error: %s\n", err);
        return -1;
    }
    return 0;
}

/* ── ROOM_QUAD ─────────────────────────────────────────────────── */

static void test_room_quad_rasterizes_correctly(void) {
    const char *input =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=20 h=16 floor_z=0 ceil_z=6 name=entrance\n"
        "SPAWN x=5 y=5 z=1\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), 0);
    ASSERT_INT_EQ(layout.room_count, (uint32_t)1);

    fr_room_def_t *r = &layout.rooms[0];
    ASSERT_INT_EQ(r->vertex_count, (uint32_t)4);
    ASSERT_FLOAT_NEAR(r->vertices[0].x, 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR(r->vertices[0].y, 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR(r->vertices[1].x, 20.0f, 0.001f);
    ASSERT_FLOAT_NEAR(r->vertices[1].y, 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR(r->vertices[2].x, 20.0f, 0.001f);
    ASSERT_FLOAT_NEAR(r->vertices[2].y, 16.0f, 0.001f);
    ASSERT_FLOAT_NEAR(r->vertices[3].x, 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR(r->vertices[3].y, 16.0f, 0.001f);
    ASSERT_FLOAT_NEAR(r->floor_z, 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR(r->ceil_z, 6.0f, 0.001f);
    ASSERT_STREQ(r->name, "entrance");
    free_layout(&layout);
    PASS();
}

static void test_room_quad_rejects_invalid_clearance(void) {
    const char *input =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=10 h=10 floor_z=5 ceil_z=3\n"
        "SPAWN x=1 y=1 z=1\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), -1);
    PASS();
}

static void test_room_quad_rejects_zero_size(void) {
    const char *input =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=-1 h=10 floor_z=0 ceil_z=6\n"
        "SPAWN x=1 y=1 z=1\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), -1);
    PASS();
}

static void test_room_quad_missing_params(void) {
    const char *input =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 floor_z=0 ceil_z=6\n"
        "SPAWN x=1 y=1 z=1\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), -1);
    PASS();
}

/* ── ROOM_PENT ─────────────────────────────────────────────────── */

static void test_room_pent_rasterizes_basic(void) {
    const char *input =
        "@grammar blockout v1\n"
        "ROOM_PENT floor_z=0 ceil_z=8 name=boss\n"
        "SPAWN x=1 y=1 z=1\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), 0);
    ASSERT_INT_EQ(layout.room_count, (uint32_t)1);
    ASSERT_INT_EQ(layout.rooms[0].vertex_count, (uint32_t)5);
    ASSERT_FLOAT_NEAR(layout.rooms[0].ceil_z, 8.0f, 0.001f);
    free_layout(&layout);
    PASS();
}

/* ── SPAWN ─────────────────────────────────────────────────────── */

static void test_spawn_position(void) {
    const char *input =
        "@grammar blockout v1\n"
        "SPAWN x=10 y=20 z=3\n"
        "ROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=6\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), 0);
    ASSERT_FLOAT_NEAR(layout.spawn_pos.x, 10.0f, 0.001f);
    ASSERT_FLOAT_NEAR(layout.spawn_pos.y, 20.0f, 0.001f);
    ASSERT_FLOAT_NEAR(layout.spawn_pos.z, 3.0f, 0.001f);
    free_layout(&layout);
    PASS();
}

static void test_missing_spawn_fails(void) {
    const char *input =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=6\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), -1);
    PASS();
}

/* ── MARKER ────────────────────────────────────────────────────── */

static void test_marker_rasterizes(void) {
    const char *input =
        "@grammar blockout v1\n"
        "SPAWN x=0 y=0 z=1\n"
        "MARKER x=10 y=5 z=1 name=treasure\n"
        "MARKER x=20 y=5 z=1 name=exit\n"
        "ROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=6\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), 0);
    ASSERT_INT_EQ(layout.marker_count, (uint32_t)2);
    ASSERT_STREQ(layout.markers[0].name, "treasure");
    ASSERT_FLOAT_NEAR(layout.markers[0].pos.x, 10.0f, 0.001f);
    ASSERT_STREQ(layout.markers[1].name, "exit");
    free_layout(&layout);
    PASS();
}

/* ── CORRIDOR ──────────────────────────────────────────────────── */

static void test_corridor_types(void) {
    const char *input =
        "@grammar blockout v1\n"
        "SPAWN x=0 y=0 z=1\n"
        "CORRIDOR_H w=6 floor_z=0 ceil_z=5\n"
        "CORRIDOR_V w=4 floor_z=0 ceil_z=5\n"
        "CORRIDOR_DIAG w=4 floor_z=0 ceil_z=5\n"
        "ROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=6\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), 0);
    ASSERT_INT_EQ(layout.corridor_count, (uint32_t)3);
    ASSERT_EQ(layout.corridors[0].angle_type, CORR_ANGLE_H);
    ASSERT_EQ(layout.corridors[1].angle_type, CORR_ANGLE_V);
    ASSERT_EQ(layout.corridors[2].angle_type, CORR_ANGLE_45);
    free_layout(&layout);
    PASS();
}

/* ── OPENING ───────────────────────────────────────────────────── */

static void test_door_and_window(void) {
    const char *input =
        "@grammar blockout v1\n"
        "SPAWN x=0 y=0 z=1\n"
        "DOOR w=3 h=4\n"
        "WINDOW w=2 h=2\n"
        "ROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=6\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), 0);
    ASSERT_INT_EQ(layout.opening_count, (uint32_t)2);
    ASSERT_EQ(layout.openings[0].type, OPEN_DOOR);
    ASSERT_EQ(layout.openings[1].type, OPEN_WINDOW);
    ASSERT_FLOAT_NEAR(layout.openings[0].width,  3.0f, 0.001f);
    ASSERT_FLOAT_NEAR(layout.openings[0].height, 4.0f, 0.001f);
    free_layout(&layout);
    PASS();
}

/* ── RAMP ──────────────────────────────────────────────────────── */

static void test_ramp_up_and_down(void) {
    const char *input =
        "@grammar blockout v1\n"
        "SPAWN x=0 y=0 z=1\n"
        "RAMP_UP dz=6 w=4\n"
        "RAMP_DOWN dz=3 w=3\n"
        "ROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=6\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), 0);
    ASSERT_INT_EQ(layout.ramp_count, (uint32_t)2);
    ASSERT_FLOAT_NEAR(layout.ramps[0].height_change,  6.0f, 0.001f);
    ASSERT_FLOAT_NEAR(layout.ramps[1].height_change, -3.0f, 0.001f);
    free_layout(&layout);
    PASS();
}

/* ── Grammar metadata ──────────────────────────────────────────── */

static void test_grammar_name_captured(void) {
    const char *input =
        "@grammar blockout v1\n"
        "SPAWN x=0 y=0 z=1\n"
        "ROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=6\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), 0);
    ASSERT_STREQ(layout.grammar_name, "blockout");
    ASSERT_INT_EQ(layout.grammar_version, (uint32_t)1);
    free_layout(&layout);
    PASS();
}

/* ── Multi-room dungeon ────────────────────────────────────────── */

static void test_multi_room_dungeon(void) {
    const char *input =
        "@grammar blockout v1\n"
        "BLOCK\n"
        "  ROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6 name=room1\n"
        "  SPAWN x=5 y=5 z=1\n"
        "  MARKER x=5 y=9 z=1 name=doorway\n"
        "  CORRIDOR_H w=4 floor_z=0 ceil_z=5\n"
        "  DOOR w=3 h=4\n"
        "  ROOM_PENT floor_z=0 ceil_z=8 name=arena\n"
        "  RAMP_UP dz=4 w=3\n"
        "  MARKER x=15 y=15 z=1 name=exit\n"
        "EBLOCK\n";
    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(tokenize_and_rasterize(input, &layout), 0);
    ASSERT_INT_EQ(layout.room_count, (uint32_t)2);
    ASSERT_INT_EQ(layout.corridor_count, (uint32_t)1);
    ASSERT_INT_EQ(layout.opening_count, (uint32_t)1);
    ASSERT_INT_EQ(layout.ramp_count, (uint32_t)1);
    ASSERT_INT_EQ(layout.marker_count, (uint32_t)2);
    free_layout(&layout);
    PASS();
}

/* ================================================================= */

int main(void) {
    printf("=== Procgen Blockout Grammar Tests ===\n\n");

    RUN(test_room_quad_rasterizes_correctly);
    RUN(test_room_quad_rejects_invalid_clearance);
    RUN(test_room_quad_rejects_zero_size);
    RUN(test_room_quad_missing_params);
    RUN(test_room_pent_rasterizes_basic);
    RUN(test_spawn_position);
    RUN(test_missing_spawn_fails);
    RUN(test_marker_rasterizes);
    RUN(test_corridor_types);
    RUN(test_door_and_window);
    RUN(test_ramp_up_and_down);
    RUN(test_grammar_name_captured);
    RUN(test_multi_room_dungeon);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
