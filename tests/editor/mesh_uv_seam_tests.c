/**
 * @file mesh_uv_seam_tests.c
 * @brief Tests for UV seam marking and clearing.
 */
#include <stdio.h>
#include "ferrum/editor/mesh/mesh_uv_seam.h"
#include "ferrum/editor/mesh/mesh_primitives.h"
#include "ferrum/editor/mesh/mesh_edit.h"

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
/* Test: mark seam edges                                               */
/* ------------------------------------------------------------------ */

static void test_mark_seam(void) {
    mesh_seam_set_t seams;
    mesh_seam_set_init(&seams);

    /* Mark edge (0,1) */
    bool ok = mesh_seam_mark(&seams, 0, 1);
    ASSERT(ok, "mark succeeded");
    ASSERT(mesh_seam_is_marked(&seams, 0, 1), "edge marked");
    /* Order-independent */
    ASSERT(mesh_seam_is_marked(&seams, 1, 0), "reverse also marked");

    mesh_seam_set_destroy(&seams);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: clear seam edges                                              */
/* ------------------------------------------------------------------ */

static void test_clear_seam(void) {
    mesh_seam_set_t seams;
    mesh_seam_set_init(&seams);

    mesh_seam_mark(&seams, 2, 5);
    ASSERT(mesh_seam_is_marked(&seams, 2, 5), "initially marked");

    mesh_seam_clear(&seams, 2, 5);
    ASSERT(!mesh_seam_is_marked(&seams, 2, 5), "cleared");

    mesh_seam_set_destroy(&seams);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: multiple seams                                                */
/* ------------------------------------------------------------------ */

static void test_multiple_seams(void) {
    mesh_seam_set_t seams;
    mesh_seam_set_init(&seams);

    mesh_seam_mark(&seams, 0, 1);
    mesh_seam_mark(&seams, 1, 2);
    mesh_seam_mark(&seams, 3, 4);

    ASSERT(mesh_seam_count(&seams) == 3, "3 seams marked");
    ASSERT(mesh_seam_is_marked(&seams, 0, 1), "edge 0-1");
    ASSERT(mesh_seam_is_marked(&seams, 1, 2), "edge 1-2");
    ASSERT(mesh_seam_is_marked(&seams, 3, 4), "edge 3-4");
    ASSERT(!mesh_seam_is_marked(&seams, 0, 2), "edge 0-2 not marked");

    mesh_seam_set_destroy(&seams);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: clear all                                                     */
/* ------------------------------------------------------------------ */

static void test_clear_all(void) {
    mesh_seam_set_t seams;
    mesh_seam_set_init(&seams);

    mesh_seam_mark(&seams, 0, 1);
    mesh_seam_mark(&seams, 2, 3);
    mesh_seam_mark(&seams, 4, 5);
    ASSERT(mesh_seam_count(&seams) == 3, "3 seams");

    mesh_seam_set_clear_all(&seams);
    ASSERT(mesh_seam_count(&seams) == 0, "cleared to 0");
    ASSERT(!mesh_seam_is_marked(&seams, 0, 1), "no longer marked");

    mesh_seam_set_destroy(&seams);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_seam_mark(NULL, 0, 1), "null mark");
    ASSERT(!mesh_seam_is_marked(NULL, 0, 1), "null is_marked");
    mesh_seam_clear(NULL, 0, 1);  /* should not crash */
    g_pass++;
}

int main(void) {
    printf("mesh_uv_seam_tests:\n");
    test_mark_seam();
    test_clear_seam();
    test_multiple_seams();
    test_clear_all();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
