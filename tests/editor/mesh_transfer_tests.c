/**
 * @file mesh_transfer_tests.c
 * @brief Tests for mesh copy, paste, clear, and OBJ import/export.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include "ferrum/editor/mesh/mesh_transfer.h"
#include "ferrum/editor/mesh/mesh_primitives.h"

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
/* Test: deep copy                                                     */
/* ------------------------------------------------------------------ */

static void test_copy(void) {
    mesh_slot_t src, dst;
    mesh_slot_init(&src, 256, 1024);
    mesh_slot_init(&dst, 8, 24);

    mesh_prim_box(&src, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    bool ok = mesh_copy(&src, &dst);
    ASSERT(ok, "copy succeeded");
    ASSERT(dst.vertex_count == src.vertex_count, "vertex count matches");
    ASSERT(dst.index_count == src.index_count, "index count matches");

    /* Verify position data matches */
    for (uint32_t v = 0; v < src.vertex_count; v++) {
        for (int k = 0; k < 3; k++) {
            ASSERT(fabsf(dst.positions[v*3+k] - src.positions[v*3+k]) < 0.01f,
                   "position matches");
        }
    }

    mesh_slot_destroy(&src);
    mesh_slot_destroy(&dst);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: paste (append)                                                */
/* ------------------------------------------------------------------ */

static void test_paste_append(void) {
    mesh_slot_t dst, clip;
    mesh_slot_init(&dst, 256, 1024);
    mesh_slot_init(&clip, 256, 1024);

    mesh_prim_box(&dst, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});
    mesh_prim_box(&clip, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){2,0,0});

    uint32_t orig_vc = dst.vertex_count;
    uint32_t orig_ic = dst.index_count;

    bool ok = mesh_paste(&clip, &dst);
    ASSERT(ok, "paste succeeded");
    ASSERT(dst.vertex_count == orig_vc + clip.vertex_count, "verts appended");
    ASSERT(dst.index_count == orig_ic + clip.index_count, "indices appended");

    mesh_slot_destroy(&dst);
    mesh_slot_destroy(&clip);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: OBJ export + import round-trip                                */
/* ------------------------------------------------------------------ */

static void test_obj_round_trip(void) {
    mesh_slot_t original, loaded;
    mesh_slot_init(&original, 256, 1024);
    mesh_slot_init(&loaded, 256, 1024);

    mesh_prim_box(&original, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    const char *path = "/tmp/test_mesh_rt.obj";
    bool ok = mesh_export_obj(&original, path);
    ASSERT(ok, "export succeeded");

    ok = mesh_import_obj(&loaded, path);
    ASSERT(ok, "import succeeded");

    /* Face count should match; vertex count may differ due to
     * OBJ per-face-corner vertex expansion vs shared vertices. */
    uint32_t orig_fc = original.index_count / 3;
    uint32_t load_fc = loaded.index_count / 3;
    ASSERT(load_fc == orig_fc, "face count match");
    ASSERT(loaded.vertex_count > 0, "has vertices");

    unlink(path);
    mesh_slot_destroy(&original);
    mesh_slot_destroy(&loaded);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: OBJ import with normals                                       */
/* ------------------------------------------------------------------ */

static void test_obj_normals(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);

    /* Write a simple OBJ with normals */
    const char *path = "/tmp/test_mesh_nrm.obj";
    FILE *fp = fopen(path, "w");
    ASSERT(fp != NULL, "create obj");
    fprintf(fp, "v 0 0 0\nv 1 0 0\nv 1 1 0\n");
    fprintf(fp, "vn 0 0 1\n");
    fprintf(fp, "f 1//1 2//1 3//1\n");
    fclose(fp);

    bool ok = mesh_import_obj(&slot, path);
    ASSERT(ok, "import with normals");
    ASSERT(slot.vertex_count == 3, "3 vertices");
    ASSERT(slot.index_count == 3, "1 face");

    /* Check normals */
    for (int v = 0; v < 3; v++) {
        ASSERT(fabsf(slot.normals[v*3+2] - 1.0f) < 0.01f, "normal z=1");
    }

    unlink(path);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_copy(NULL, NULL), "null copy");
    ASSERT(!mesh_paste(NULL, NULL), "null paste");
    ASSERT(!mesh_export_obj(NULL, NULL), "null export");
    ASSERT(!mesh_import_obj(NULL, NULL), "null import");
    g_pass++;
}

int main(void) {
    printf("mesh_transfer_tests:\n");
    test_copy();
    test_paste_append();
    test_obj_round_trip();
    test_obj_normals();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
