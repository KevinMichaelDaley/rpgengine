/**
 * @file ctrl_mesh_mode_tests.c
 * @brief Tests for mesh mode state machine and keybinding dispatch.
 */
#include <stdio.h>
#include <string.h>
#include "ferrum/editor/ctrl_mesh_mode.h"

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

/* ------------------------------------------------------------------ */
/* Test: mode switching with number keys                               */
/* ------------------------------------------------------------------ */

static void test_mode_switch(void) {
    ctrl_mesh_mode_t mm;
    ctrl_mesh_mode_init(&mm);
    ASSERT(mm.sel_mode == MESH_SEL_MODE_VERTEX, "starts vertex mode");

    ctrl_mesh_mode_set_sel_mode(&mm, MESH_SEL_MODE_FACE);
    ASSERT(mm.sel_mode == MESH_SEL_MODE_FACE, "switched to face");

    ctrl_mesh_mode_set_sel_mode(&mm, MESH_SEL_MODE_EDGE);
    ASSERT(mm.sel_mode == MESH_SEL_MODE_EDGE, "switched to edge");

    ctrl_mesh_mode_set_sel_mode(&mm, MESH_SEL_MODE_OBJECT);
    ASSERT(mm.sel_mode == MESH_SEL_MODE_OBJECT, "switched to object");

    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: key dispatch for face mode keys                               */
/* ------------------------------------------------------------------ */

static void test_face_mode_keys(void) {
    ctrl_mesh_mode_t mm;
    ctrl_mesh_mode_init(&mm);
    ctrl_mesh_mode_set_sel_mode(&mm, MESH_SEL_MODE_FACE);

    const char *cmd;
    cmd = ctrl_mesh_mode_key_to_command(&mm, 'e');
    ASSERT(cmd != NULL && strcmp(cmd, "extrude") == 0, "'e' -> extrude in face mode");

    cmd = ctrl_mesh_mode_key_to_command(&mm, 'i');
    ASSERT(cmd != NULL && strcmp(cmd, "inset") == 0, "'i' -> inset in face mode");

    cmd = ctrl_mesh_mode_key_to_command(&mm, 'g');
    ASSERT(cmd != NULL && strcmp(cmd, "grow_selection") == 0, "'g' -> grow in face mode");

    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: key dispatch for edge mode keys                               */
/* ------------------------------------------------------------------ */

static void test_edge_mode_keys(void) {
    ctrl_mesh_mode_t mm;
    ctrl_mesh_mode_init(&mm);
    ctrl_mesh_mode_set_sel_mode(&mm, MESH_SEL_MODE_EDGE);

    const char *cmd;
    cmd = ctrl_mesh_mode_key_to_command(&mm, 'b');
    ASSERT(cmd != NULL && strcmp(cmd, "bevel") == 0, "'b' -> bevel in edge mode");

    cmd = ctrl_mesh_mode_key_to_command(&mm, 'c');
    ASSERT(cmd != NULL && strcmp(cmd, "loop_cut") == 0, "'c' -> loop_cut in edge mode");

    cmd = ctrl_mesh_mode_key_to_command(&mm, 'x');
    ASSERT(cmd != NULL && strcmp(cmd, "edge_ring") == 0, "'x' -> edge_ring in edge mode");

    cmd = ctrl_mesh_mode_key_to_command(&mm, 'l');
    ASSERT(cmd != NULL && strcmp(cmd, "edge_loop") == 0, "'l' -> edge_loop in edge mode");

    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: status text generation                                        */
/* ------------------------------------------------------------------ */

static void test_status_text(void) {
    ctrl_mesh_mode_t mm;
    ctrl_mesh_mode_init(&mm);
    mm.sel_mode = MESH_SEL_MODE_FACE;
    mm.sel_count = 12;
    mm.vertex_count = 24;
    mm.tri_count = 12;

    char buf[256];
    ctrl_mesh_mode_status(&mm, buf, sizeof(buf));
    ASSERT(strlen(buf) > 0, "status not empty");
    /* Should contain mode name and counts */
    ASSERT(strstr(buf, "face") != NULL || strstr(buf, "FACE") != NULL,
           "status mentions mode");

    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: toggle flags                                                  */
/* ------------------------------------------------------------------ */

static void test_toggles(void) {
    ctrl_mesh_mode_t mm;
    ctrl_mesh_mode_init(&mm);

    ASSERT(!mm.wireframe, "wireframe off initially");
    ctrl_mesh_mode_toggle_wireframe(&mm);
    ASSERT(mm.wireframe, "wireframe on after toggle");
    ctrl_mesh_mode_toggle_wireframe(&mm);
    ASSERT(!mm.wireframe, "wireframe off after second toggle");

    ASSERT(!mm.xray, "xray off initially");
    ctrl_mesh_mode_toggle_xray(&mm);
    ASSERT(mm.xray, "xray on after toggle");

    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: key in wrong mode returns NULL                                */
/* ------------------------------------------------------------------ */

static void test_wrong_mode_key(void) {
    ctrl_mesh_mode_t mm;
    ctrl_mesh_mode_init(&mm);
    mm.sel_mode = MESH_SEL_MODE_VERTEX;

    /* Face-only keys should not dispatch in vertex mode */
    const char *cmd = ctrl_mesh_mode_key_to_command(&mm, 'e');
    ASSERT(cmd == NULL, "'e' has no binding in vertex mode");

    /* Edge-only keys should not dispatch in vertex mode */
    cmd = ctrl_mesh_mode_key_to_command(&mm, 'b');
    ASSERT(cmd == NULL, "'b' has no binding in vertex mode");

    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(ctrl_mesh_mode_key_to_command(NULL, 'e') == NULL, "null key dispatch");
    char buf[64];
    ctrl_mesh_mode_status(NULL, buf, sizeof(buf));
    ASSERT(buf[0] == '\0', "null status empty");
    g_pass++;
}

int main(void) {
    printf("ctrl_mesh_mode_tests:\n");
    test_mode_switch();
    test_face_mode_keys();
    test_edge_mode_keys();
    test_status_text();
    test_toggles();
    test_wrong_mode_key();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
