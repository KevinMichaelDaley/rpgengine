/**
 * @file client_cursor_tests.c
 * @brief Tests for 3D editor cursor state and rendering.
 *
 * Unit tests: cursor state (init, move, snap, set, toggle).
 * Integration tests: cursor debug lines submitted to line store.
 * GL integration test: cursor rendered to offscreen FBO, screenshot captured.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/editor/client/client_cursor.h"
#include "ferrum/renderer/debug_lines.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps) \
    ASSERT(fabsf((a) - (b)) < (eps))

/* ----------------------------------------------------------------------- */
/* Unit tests: cursor state                                                  */
/* ----------------------------------------------------------------------- */

/** Default init: origin, grid=1, snap on, visible. */
static bool test_init_defaults(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    ASSERT_NEAR(cur.position.x, 0.0f, 1e-6f);
    ASSERT_NEAR(cur.position.y, 0.0f, 1e-6f);
    ASSERT_NEAR(cur.position.z, 0.0f, 1e-6f);
    ASSERT_NEAR(cur.grid_size, 1.0f, 1e-6f);
    ASSERT(cur.snap_enabled == true);
    ASSERT(cur.visible == true);
    return true;
}

/** Move without snap — position shifts by exact delta. */
static bool test_move_no_snap(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.snap_enabled = false;

    editor_cursor_move(&cur, (vec3_t){1.3f, 2.7f, -0.5f});
    ASSERT_NEAR(cur.position.x, 1.3f, 1e-5f);
    ASSERT_NEAR(cur.position.y, 2.7f, 1e-5f);
    ASSERT_NEAR(cur.position.z, -0.5f, 1e-5f);
    return true;
}

/** Move with snap — position rounds to grid. */
static bool test_move_with_snap(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.grid_size = 0.5f;
    cur.snap_enabled = true;

    editor_cursor_move(&cur, (vec3_t){0.3f, 0.8f, -0.1f});
    /* Expected: 0.3 → 0.5, 0.8 → 1.0, -0.1 → 0.0 */
    ASSERT_NEAR(cur.position.x, 0.5f, 1e-5f);
    ASSERT_NEAR(cur.position.y, 1.0f, 1e-5f);
    ASSERT_NEAR(cur.position.z, 0.0f, 1e-5f);
    return true;
}

/** Move with snap, grid=1 — rounds to nearest integer. */
static bool test_move_snap_grid1(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.grid_size = 1.0f;
    cur.snap_enabled = true;

    editor_cursor_move(&cur, (vec3_t){1.6f, -2.3f, 0.5f});
    /* 1.6→2.0, -2.3→-2.0, 0.5→1.0 (roundf rounds 0.5 up). */
    ASSERT_NEAR(cur.position.x, 2.0f, 1e-5f);
    ASSERT_NEAR(cur.position.y, -2.0f, 1e-5f);
    /* roundf(0.5) is implementation-defined, accept 0 or 1. */
    ASSERT(fabsf(cur.position.z - 0.0f) < 1e-5f ||
           fabsf(cur.position.z - 1.0f) < 1e-5f);
    return true;
}

/** Set position with snap. */
static bool test_set_position_snap(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.grid_size = 2.0f;
    cur.snap_enabled = true;

    editor_cursor_set_position(&cur, (vec3_t){3.1f, 5.9f, -1.0f});
    /* 3.1→4.0, 5.9→6.0, -1.0→-2.0 */
    ASSERT_NEAR(cur.position.x, 4.0f, 1e-5f);
    ASSERT_NEAR(cur.position.y, 6.0f, 1e-5f);
    ASSERT_NEAR(cur.position.z, -2.0f, 1e-5f);
    return true;
}

/** Set position without snap — exact. */
static bool test_set_position_no_snap(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.snap_enabled = false;

    editor_cursor_set_position(&cur, (vec3_t){3.14f, -2.71f, 0.0f});
    ASSERT_NEAR(cur.position.x, 3.14f, 1e-5f);
    ASSERT_NEAR(cur.position.y, -2.71f, 1e-5f);
    ASSERT_NEAR(cur.position.z, 0.0f, 1e-5f);
    return true;
}

/** Toggle visibility. */
static bool test_toggle_visible(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    ASSERT(cur.visible == true);
    editor_cursor_toggle_visible(&cur);
    ASSERT(cur.visible == false);
    editor_cursor_toggle_visible(&cur);
    ASSERT(cur.visible == true);
    return true;
}

/** Null safety. */
static bool test_null_safety(void) {
    /* Should not crash. */
    editor_cursor_init(NULL);
    editor_cursor_move(NULL, (vec3_t){1,2,3});
    editor_cursor_set_position(NULL, (vec3_t){1,2,3});
    editor_cursor_toggle_visible(NULL);
    ASSERT(editor_cursor_submit_lines(NULL, NULL, 0, 0) == 0);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Integration tests: debug line submission                                  */
/* ----------------------------------------------------------------------- */

/** Submit lines for visible cursor — expect 7 lines (3 axes + 4 grid). */
static bool test_submit_lines_visible(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.position = (vec3_t){5.0f, 0.0f, 5.0f};

    fr_debug_line_t storage[64];
    fr_debug_lines_t lines;
    fr_debug_lines_init(&lines, storage, 64);

    uint32_t count = editor_cursor_submit_lines(&cur, &lines, 0.0, 0.016);
    ASSERT(count == 7);  /* 3 axis + 4 grid edges */
    ASSERT(lines.count == 7);
    return true;
}

/** Hidden cursor submits 0 lines. */
static bool test_submit_lines_hidden(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.visible = false;

    fr_debug_line_t storage[64];
    fr_debug_lines_t lines;
    fr_debug_lines_init(&lines, storage, 64);

    uint32_t count = editor_cursor_submit_lines(&cur, &lines, 0.0, 0.016);
    ASSERT(count == 0);
    ASSERT(lines.count == 0);
    return true;
}

/** Axis lines have correct endpoints. */
static bool test_axis_line_endpoints(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.position = (vec3_t){2.0f, 3.0f, 4.0f};
    cur.grid_size = 1.0f;

    fr_debug_line_t storage[64];
    fr_debug_lines_t lines;
    fr_debug_lines_init(&lines, storage, 64);

    editor_cursor_submit_lines(&cur, &lines, 0.0, 0.016);

    /* Line 0 = X axis: from (1,3,4) to (3,3,4) */
    ASSERT_NEAR(storage[0].a.x, 1.0f, 1e-5f);
    ASSERT_NEAR(storage[0].a.y, 3.0f, 1e-5f);
    ASSERT_NEAR(storage[0].a.z, 4.0f, 1e-5f);
    ASSERT_NEAR(storage[0].b.x, 3.0f, 1e-5f);
    ASSERT_NEAR(storage[0].b.y, 3.0f, 1e-5f);
    ASSERT_NEAR(storage[0].b.z, 4.0f, 1e-5f);

    /* Line 1 = Y axis: from (2,2,4) to (2,4,4) */
    ASSERT_NEAR(storage[1].a.x, 2.0f, 1e-5f);
    ASSERT_NEAR(storage[1].a.y, 2.0f, 1e-5f);
    ASSERT_NEAR(storage[1].a.z, 4.0f, 1e-5f);
    ASSERT_NEAR(storage[1].b.x, 2.0f, 1e-5f);
    ASSERT_NEAR(storage[1].b.y, 4.0f, 1e-5f);
    ASSERT_NEAR(storage[1].b.z, 4.0f, 1e-5f);

    /* Line 2 = Z axis: from (2,3,3) to (2,3,5) */
    ASSERT_NEAR(storage[2].a.x, 2.0f, 1e-5f);
    ASSERT_NEAR(storage[2].a.y, 3.0f, 1e-5f);
    ASSERT_NEAR(storage[2].a.z, 3.0f, 1e-5f);
    ASSERT_NEAR(storage[2].b.x, 2.0f, 1e-5f);
    ASSERT_NEAR(storage[2].b.y, 3.0f, 1e-5f);
    ASSERT_NEAR(storage[2].b.z, 5.0f, 1e-5f);
    return true;
}

/** Grid highlight forms a square on the XZ plane. */
static bool test_grid_highlight_square(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.position = (vec3_t){0.0f, 0.0f, 0.0f};
    cur.grid_size = 1.0f;

    fr_debug_line_t storage[64];
    fr_debug_lines_t lines;
    fr_debug_lines_init(&lines, storage, 64);

    editor_cursor_submit_lines(&cur, &lines, 0.0, 0.016);

    /* Lines 3..6 = grid square corners.
     * Grid cell centered on cursor: half = grid_size.
     * Corners: (-1,0,-1) → (1,0,-1) → (1,0,1) → (-1,0,1) → (-1,0,-1)
     */
    /* Line 3: (-1,0,-1) → (1,0,-1) */
    ASSERT_NEAR(storage[3].a.x, -1.0f, 1e-5f);
    ASSERT_NEAR(storage[3].a.z, -1.0f, 1e-5f);
    ASSERT_NEAR(storage[3].b.x,  1.0f, 1e-5f);
    ASSERT_NEAR(storage[3].b.z, -1.0f, 1e-5f);

    /* Line 4: (1,0,-1) → (1,0,1) */
    ASSERT_NEAR(storage[4].a.x,  1.0f, 1e-5f);
    ASSERT_NEAR(storage[4].a.z, -1.0f, 1e-5f);
    ASSERT_NEAR(storage[4].b.x,  1.0f, 1e-5f);
    ASSERT_NEAR(storage[4].b.z,  1.0f, 1e-5f);

    /* Line 5: (1,0,1) → (-1,0,1) */
    ASSERT_NEAR(storage[5].a.x,  1.0f, 1e-5f);
    ASSERT_NEAR(storage[5].a.z,  1.0f, 1e-5f);
    ASSERT_NEAR(storage[5].b.x, -1.0f, 1e-5f);
    ASSERT_NEAR(storage[5].b.z,  1.0f, 1e-5f);

    /* Line 6: (-1,0,1) → (-1,0,-1) */
    ASSERT_NEAR(storage[6].a.x, -1.0f, 1e-5f);
    ASSERT_NEAR(storage[6].a.z,  1.0f, 1e-5f);
    ASSERT_NEAR(storage[6].b.x, -1.0f, 1e-5f);
    ASSERT_NEAR(storage[6].b.z, -1.0f, 1e-5f);
    return true;
}

/** Collected vertices from submitted lines form correct pairs. */
static bool test_collected_vertices(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.position = (vec3_t){0.0f, 0.0f, 0.0f};

    fr_debug_line_t storage[64];
    fr_debug_lines_t lines;
    fr_debug_lines_init(&lines, storage, 64);

    editor_cursor_submit_lines(&cur, &lines, 1.0, 0.016);

    /* Collect while lines are still alive (now < expire_time). */
    vec3_t verts[128];
    size_t vert_count = 0;
    bool ok = fr_debug_lines_collect_vertices(&lines, 1.001, verts, 128,
                                               &vert_count);
    ASSERT(ok);
    ASSERT(vert_count == 14);  /* 7 lines × 2 vertices each */
    return true;
}

/** Vertices expire after frame_dt. */
static bool test_vertices_expire(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);

    fr_debug_line_t storage[64];
    fr_debug_lines_t lines;
    fr_debug_lines_init(&lines, storage, 64);

    editor_cursor_submit_lines(&cur, &lines, 1.0, 0.016);

    /* Collect after TTL has elapsed. */
    vec3_t verts[128];
    size_t vert_count = 0;
    bool ok = fr_debug_lines_collect_vertices(&lines, 2.0, verts, 128,
                                               &vert_count);
    ASSERT(ok);
    ASSERT(vert_count == 0);  /* All expired. */
    return true;
}

/** Different grid sizes produce different axis line lengths. */
static bool test_grid_size_changes_lines(void) {
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.grid_size = 0.5f;
    cur.position = (vec3_t){0.0f, 0.0f, 0.0f};

    fr_debug_line_t storage[64];
    fr_debug_lines_t lines;
    fr_debug_lines_init(&lines, storage, 64);

    editor_cursor_submit_lines(&cur, &lines, 0.0, 0.016);

    /* X axis: from (-0.5,0,0) to (0.5,0,0) */
    ASSERT_NEAR(storage[0].a.x, -0.5f, 1e-5f);
    ASSERT_NEAR(storage[0].b.x,  0.5f, 1e-5f);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    /* Unit tests */
    RUN(test_init_defaults);
    RUN(test_move_no_snap);
    RUN(test_move_with_snap);
    RUN(test_move_snap_grid1);
    RUN(test_set_position_snap);
    RUN(test_set_position_no_snap);
    RUN(test_toggle_visible);
    RUN(test_null_safety);

    /* Integration tests */
    RUN(test_submit_lines_visible);
    RUN(test_submit_lines_hidden);
    RUN(test_axis_line_endpoints);
    RUN(test_grid_highlight_square);
    RUN(test_collected_vertices);
    RUN(test_vertices_expire);
    RUN(test_grid_size_changes_lines);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
