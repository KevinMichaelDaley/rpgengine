/**
 * @file procgen_types_tests.c
 * @brief RED: Tests for procgen core type definitions.
 *
 * Validates: enum values, struct sizes, field offsets, header integrity.
 * Tests should FAIL until types are finalized.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/math/vec3.h"
#include "ferrum/procgen/procgen_types.h"
#include "ferrum/procgen/procgen_layout.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn)                                                              \
    do {                                                                     \
        printf("RUN  %s\n", #fn);                                           \
        fn();                                                                \
        printf("OK   %s\n", #fn);                                           \
    } while (0)

#define ASSERT_TRUE(expr)                                                    \
    do {                                                                     \
        if (!(expr)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);           \
            g_fail++;                                                        \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_EQ(a, b)          ASSERT_TRUE((a) == (b))
#define ASSERT_INT_EQ(a, b)      ASSERT_TRUE((int)(a) == (int)(b))
#define ASSERT_SIZE_EQ(a, b)     ASSERT_TRUE((size_t)(a) == (size_t)(b))
#define PASS()                   g_pass++

/* ── Token type enum ───────────────────────────────────────────── */

static void test_tok_enum_values_distinct(void) {
    /* Every valid token type must have a unique value. */
    ASSERT_TRUE(TOK_ROOM_QUAD != TOK_ROOM_PENT);
    ASSERT_TRUE(TOK_ROOM_QUAD != TOK_CORRIDOR_H);
    ASSERT_TRUE(TOK_CORRIDOR_H != TOK_CORRIDOR_V);
    ASSERT_TRUE(TOK_CORRIDOR_H != TOK_CORRIDOR_DIAG);
    ASSERT_TRUE(TOK_RAMP_UP != TOK_RAMP_DOWN);
    ASSERT_TRUE(TOK_DOOR != TOK_WINDOW);
    ASSERT_TRUE(TOK_SPAWN != TOK_MARKER);
    ASSERT_TRUE(TOK_BLOCK != TOK_EBLOCK);
    PASS();
}

static void test_tok_enum_has_required_tokens(void) {
    /* All token types required by the blockout grammar exist. */
    volatile tok_type_t t;

    t = TOK_ROOM_QUAD;     (void)t;
    t = TOK_ROOM_PENT;     (void)t;
    t = TOK_CORRIDOR_H;    (void)t;
    t = TOK_CORRIDOR_V;    (void)t;
    t = TOK_CORRIDOR_DIAG; (void)t;
    t = TOK_RAMP_UP;       (void)t;
    t = TOK_RAMP_DOWN;     (void)t;
    t = TOK_DOOR;          (void)t;
    t = TOK_WINDOW;        (void)t;
    t = TOK_SPAWN;         (void)t;
    t = TOK_MARKER;        (void)t;
    t = TOK_BLOCK;         (void)t;
    t = TOK_EBLOCK;        (void)t;
    t = TOK_GRAMMAR;       (void)t;
    t = TOK_ERROR;         (void)t;
    PASS();
}

/* ── Token struct ──────────────────────────────────────────────── */

static void test_token_struct_size(void) {
    /* Token must be reasonably sized for stack/buffer use. */
    ASSERT_TRUE(sizeof(procgen_token_t) <= 128);
    PASS();
}

static void test_token_has_required_fields(void) {
    procgen_token_t tok;

    /* Integer value via union. */
    tok.type    = TOK_SPAWN;
    tok.line    = 4;
    tok.col     = 12;
    tok.value.i = 42;
    ASSERT_EQ(tok.type, TOK_SPAWN);
    ASSERT_EQ(tok.line, (uint32_t)4);
    ASSERT_EQ(tok.col,  (uint32_t)12);
    ASSERT_INT_EQ(tok.value.i, 42);

    /* Float value via union. */
    tok.value.f = 3.14f;
    ASSERT_TRUE(tok.value.f >= 3.13f && tok.value.f <= 3.15f);

    /* String value via union. */
    tok.value.s[0] = 't';
    tok.value.s[1] = 'e';
    tok.value.s[2] = 's';
    tok.value.s[3] = 't';
    tok.value.s[4] = '\0';
    ASSERT_TRUE(tok.value.s[0] == 't');
    ASSERT_TRUE(tok.value.s[1] == 'e');
    PASS();
}

/* ── Room definition ───────────────────────────────────────────── */

static void test_room_def_has_required_fields(void) {
    fr_room_def_t room;
    room.vertex_count = 4;
    room.floor_z      = 0.0f;
    room.ceil_z       = 6.0f;
    /* name */
    room.name[0] = 't';
    room.name[1] = 'e';
    room.name[2] = 's';
    room.name[3] = 't';
    room.name[4] = '\0';

    ASSERT_EQ(room.vertex_count, (uint32_t)4);
    ASSERT_TRUE(room.floor_z == 0.0f);
    ASSERT_TRUE(room.ceil_z  == 6.0f);
    ASSERT_TRUE(room.name[0] == 't');
    PASS();
}

static void test_room_def_max_vertices(void) {
    /* Must support at least 8 vertices (future hex rooms). */
    ASSERT_TRUE(sizeof(((fr_room_def_t *)0)->vertices) / sizeof(vec3_t) >= 8);
    PASS();
}

/* ── Corridor definition ───────────────────────────────────────── */

static void test_corridor_def_angle_enum(void) {
    ASSERT_TRUE(CORR_ANGLE_H      != CORR_ANGLE_V);
    ASSERT_TRUE(CORR_ANGLE_V      != CORR_ANGLE_45);
    ASSERT_TRUE(CORR_ANGLE_45     != CORR_ANGLE_30_60);
    PASS();
}

static void test_corridor_def_has_required_fields(void) {
    fr_corridor_def_t corr;
    corr.from       = (vec3_t){0.0f, 0.0f, 0.0f};
    corr.to         = (vec3_t){10.0f, 0.0f, 0.0f};
    corr.width      = 4.0f;
    corr.floor_z    = 0.0f;
    corr.ceil_z     = 5.0f;
    corr.angle_type = CORR_ANGLE_H;

    ASSERT_TRUE(corr.from.x == 0.0f);
    ASSERT_TRUE(corr.to.x   == 10.0f);
    ASSERT_TRUE(corr.width  == 4.0f);
    ASSERT_EQ(corr.angle_type, CORR_ANGLE_H);
    PASS();
}

/* ── Opening definition ────────────────────────────────────────── */

static void test_opening_def_type_enum(void) {
    ASSERT_TRUE(OPEN_DOOR != OPEN_WINDOW);
    PASS();
}

static void test_opening_def_has_required_fields(void) {
    fr_opening_def_t op;
    op.pos    = (vec3_t){5.0f, 0.0f, 1.0f};
    op.width  = 2.0f;
    op.height = 3.0f;
    op.type   = OPEN_DOOR;

    ASSERT_TRUE(op.pos.x  == 5.0f);
    ASSERT_TRUE(op.width  == 2.0f);
    ASSERT_TRUE(op.height == 3.0f);
    ASSERT_EQ(op.type, OPEN_DOOR);
    PASS();
}

/* ── Ramp definition ───────────────────────────────────────────── */

static void test_ramp_def_has_required_fields(void) {
    fr_ramp_def_t ramp;
    ramp.from          = (vec3_t){0.0f, 0.0f, 0.0f};
    ramp.to            = (vec3_t){10.0f, 0.0f, 6.0f};
    ramp.height_change = 6.0f;
    ramp.width         = 4.0f;

    ASSERT_TRUE(ramp.height_change == 6.0f);
    ASSERT_TRUE(ramp.width         == 4.0f);
    PASS();
}

/* ── Marker definition ─────────────────────────────────────────── */

static void test_marker_def_has_required_fields(void) {
    fr_marker_def_t m;
    m.pos      = (vec3_t){10.0f, 5.0f, 1.0f};
    m.name[0]  = 'b';
    m.name[1]  = 'o';
    m.name[2]  = 's';
    m.name[3]  = 's';
    m.name[4]  = '\0';

    ASSERT_TRUE(m.pos.x   == 10.0f);
    ASSERT_TRUE(m.pos.y   == 5.0f);
    ASSERT_TRUE(m.name[0] == 'b');
    PASS();
}

/* ── Nav graph ─────────────────────────────────────────────────── */

static void test_nav_node_type_enum(void) {
    ASSERT_TRUE(NAV_ROOM != NAV_JUNCTION);
    PASS();
}

static void test_nav_node_has_required_fields(void) {
    fr_nav_node_t node;
    node.type       = NAV_ROOM;
    node.pos        = (vec3_t){0.0f, 0.0f, 0.0f};
    node.room_index = 3;

    ASSERT_EQ(node.type, NAV_ROOM);
    ASSERT_EQ(node.room_index, (uint32_t)3);
    PASS();
}

static void test_nav_edge_has_required_fields(void) {
    fr_nav_edge_t edge;
    edge.from_node = 0;
    edge.to_node   = 1;
    edge.distance  = 15.5f;

    ASSERT_EQ(edge.from_node, (uint32_t)0);
    ASSERT_EQ(edge.to_node,   (uint32_t)1);
    ASSERT_TRUE(edge.distance == 15.5f);
    PASS();
}

/* ── Dungeon layout ────────────────────────────────────────────── */

static void test_dungeon_layout_has_required_fields(void) {
    fr_dungeon_layout_t layout;
    layout.version         = 1;
    layout.room_count      = 0;
    layout.rooms           = NULL;
    layout.corridor_count  = 0;
    layout.corridors       = NULL;
    layout.opening_count   = 0;
    layout.openings        = NULL;
    layout.ramp_count      = 0;
    layout.ramps           = NULL;
    layout.marker_count    = 0;
    layout.markers         = NULL;
    layout.spawn_pos       = (vec3_t){0.0f, 0.0f, 0.0f};
    layout.nav_node_count  = 0;
    layout.nav_nodes       = NULL;
    layout.nav_edge_count  = 0;
    layout.nav_edges       = NULL;

    ASSERT_EQ(layout.version, (uint32_t)1);
    ASSERT_EQ(layout.room_count, (uint32_t)0);
    PASS();
}

static void test_dungeon_layout_metadata_fields(void) {
    fr_dungeon_layout_t layout;
    layout.grammar_name[0]    = 'b';
    layout.grammar_name[1]    = '\0';
    layout.grammar_version    = 1;
    layout.raw_token_string[0] = 'R';
    layout.raw_token_string[1] = '\0';

    ASSERT_TRUE(layout.grammar_name[0] == 'b');
    ASSERT_EQ(layout.grammar_version, (uint32_t)1);
    ASSERT_TRUE(layout.raw_token_string[0] == 'R');
    PASS();
}

/* ── Tokenizer error codes ─────────────────────────────────────── */

static void test_tok_error_codes_defined(void) {
    volatile tok_error_t e;

    e = TOK_ERR_NONE;               (void)e;
    e = TOK_ERR_UNEXPECTED_TOKEN;   (void)e;
    e = TOK_ERR_UNBALANCED_BLOCK;   (void)e;
    e = TOK_ERR_MISSING_PARAM;      (void)e;
    e = TOK_ERR_INVALID_NUMBER;     (void)e;
    e = TOK_ERR_BUFFER_FULL;        (void)e;
    e = TOK_ERR_INTERNAL;           (void)e;
    PASS();
}

/* ── vec2i helper ──────────────────────────────────────────────── */

static void test_vec2i_fields(void) {
    fr_vec2i_t v;
    v.x = 10;
    v.y = -5;

    ASSERT_INT_EQ(v.x, 10);
    ASSERT_INT_EQ(v.y, -5);
    PASS();
}

/* ================================================================= */

int main(void) {
    printf("=== Procgen Types Tests ===\n\n");

    RUN(test_tok_enum_values_distinct);
    RUN(test_tok_enum_has_required_tokens);
    RUN(test_token_struct_size);
    RUN(test_token_has_required_fields);
    RUN(test_room_def_has_required_fields);
    RUN(test_room_def_max_vertices);
    RUN(test_corridor_def_angle_enum);
    RUN(test_corridor_def_has_required_fields);
    RUN(test_opening_def_type_enum);
    RUN(test_opening_def_has_required_fields);
    RUN(test_ramp_def_has_required_fields);
    RUN(test_marker_def_has_required_fields);
    RUN(test_nav_node_type_enum);
    RUN(test_nav_node_has_required_fields);
    RUN(test_nav_edge_has_required_fields);
    RUN(test_dungeon_layout_has_required_fields);
    RUN(test_dungeon_layout_metadata_fields);
    RUN(test_tok_error_codes_defined);
    RUN(test_vec2i_fields);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
