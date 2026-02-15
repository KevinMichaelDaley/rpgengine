/**
 * @file p110_obj_loader_tests.c
 * @brief Unit tests for the Wavefront OBJ triangle mesh loader.
 *
 * Tests:
 *   - Load a minimal inline OBJ (4 verts, 2 faces) from a temp file
 *   - Handles v//vn face format (vertex//normal index)
 *   - Handles v/vt/vn face format
 *   - Handles bare v face format (index only)
 *   - Applies uniform scale during load
 *   - Returns 0 triangles for empty or comment-only file
 *   - Returns error for NULL path
 *   - Returns error when buffer is too small
 *   - Loads real armadillo OBJ and checks triangle count
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ferrum/mesh/obj_loader.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                \
                    __FILE__, __LINE__, #cond);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(a, b)                                                       \
    do {                                                                       \
        if ((a) != (b)) {                                                      \
            fprintf(stderr, "ASSERT_EQ failed: %s:%d: %d != %d\n",            \
                    __FILE__, __LINE__, (int)(a), (int)(b));                   \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(a, b, eps)                                          \
    do {                                                                       \
        float _a = (a), _b = (b);                                             \
        if (fabsf(_a - _b) > (eps)) {                                          \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: %f != %f\n",    \
                    __FILE__, __LINE__, (double)_a, (double)_b);               \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

/** Write a string to a temp file and return its path (static buffer). */
static const char *write_temp_obj(const char *content) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/obj_test_%d.obj", (int)getpid());
    FILE *f = fopen(path, "w");
    if (!f) return NULL;
    fputs(content, f);
    fclose(f);
    return path;
}

static void cleanup_temp(const char *path) {
    if (path) remove(path);
}

/* ── Tests ──────────────────────────────────────────────────────── */

/** Load a minimal quad (2 triangles) from an inline OBJ. */
static int test_load_minimal_obj(void) {
    const char *obj =
        "# minimal quad\n"
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 1.0 1.0 0.0\n"
        "v 0.0 1.0 0.0\n"
        "f 1 2 3\n"
        "f 1 3 4\n";
    const char *path = write_temp_obj(obj);
    ASSERT_TRUE(path != NULL);

    float verts[2 * 9]; /* 2 tris × 3 verts × 3 floats */
    uint32_t tri_count = 0;
    int rc = obj_load_triangles(path, 1.0f, verts, 2, &tri_count);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(tri_count, 2);

    /* First triangle: v0=(0,0,0), v1=(1,0,0), v2=(1,1,0). */
    ASSERT_FLOAT_NEAR(verts[0], 0.0f, 1e-5f); /* v0.x */
    ASSERT_FLOAT_NEAR(verts[1], 0.0f, 1e-5f); /* v0.y */
    ASSERT_FLOAT_NEAR(verts[2], 0.0f, 1e-5f); /* v0.z */
    ASSERT_FLOAT_NEAR(verts[3], 1.0f, 1e-5f); /* v1.x */
    ASSERT_FLOAT_NEAR(verts[4], 0.0f, 1e-5f); /* v1.y */
    ASSERT_FLOAT_NEAR(verts[5], 0.0f, 1e-5f); /* v1.z */
    ASSERT_FLOAT_NEAR(verts[6], 1.0f, 1e-5f); /* v2.x */
    ASSERT_FLOAT_NEAR(verts[7], 1.0f, 1e-5f); /* v2.y */

    /* Second triangle: v0=(0,0,0), v1=(1,1,0), v2=(0,1,0). */
    ASSERT_FLOAT_NEAR(verts[9],  0.0f, 1e-5f); /* v0.x */
    ASSERT_FLOAT_NEAR(verts[12], 1.0f, 1e-5f); /* v1.x of tri 2 */

    cleanup_temp(path);
    return 0;
}

/** Handle v//vn face format (used by the Stanford armadillo). */
static int test_load_vertex_normal_format(void) {
    const char *obj =
        "v 0.0 0.0 0.0\n"
        "v 2.0 0.0 0.0\n"
        "v 2.0 2.0 0.0\n"
        "vn 0.0 0.0 1.0\n"
        "f 1//1 2//1 3//1\n";
    const char *path = write_temp_obj(obj);
    ASSERT_TRUE(path != NULL);

    float verts[9];
    uint32_t tri_count = 0;
    int rc = obj_load_triangles(path, 1.0f, verts, 1, &tri_count);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(tri_count, 1);
    ASSERT_FLOAT_NEAR(verts[3], 2.0f, 1e-5f); /* v1.x */

    cleanup_temp(path);
    return 0;
}

/** Handle v/vt/vn face format. */
static int test_load_full_face_format(void) {
    const char *obj =
        "v 0.0 0.0 0.0\n"
        "v 3.0 0.0 0.0\n"
        "v 3.0 3.0 0.0\n"
        "vt 0.0 0.0\n"
        "vn 0.0 0.0 1.0\n"
        "f 1/1/1 2/1/1 3/1/1\n";
    const char *path = write_temp_obj(obj);
    ASSERT_TRUE(path != NULL);

    float verts[9];
    uint32_t tri_count = 0;
    int rc = obj_load_triangles(path, 1.0f, verts, 1, &tri_count);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(tri_count, 1);
    ASSERT_FLOAT_NEAR(verts[3], 3.0f, 1e-5f);

    cleanup_temp(path);
    return 0;
}

/** Uniform scale is applied to all vertices. */
static int test_load_with_scale(void) {
    const char *obj =
        "v 1.0 2.0 3.0\n"
        "v 4.0 5.0 6.0\n"
        "v 7.0 8.0 9.0\n"
        "f 1 2 3\n";
    const char *path = write_temp_obj(obj);
    ASSERT_TRUE(path != NULL);

    float verts[9];
    uint32_t tri_count = 0;
    int rc = obj_load_triangles(path, 2.0f, verts, 1, &tri_count);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(tri_count, 1);
    ASSERT_FLOAT_NEAR(verts[0], 2.0f, 1e-5f);
    ASSERT_FLOAT_NEAR(verts[1], 4.0f, 1e-5f);
    ASSERT_FLOAT_NEAR(verts[2], 6.0f, 1e-5f);
    ASSERT_FLOAT_NEAR(verts[3], 8.0f, 1e-5f);

    cleanup_temp(path);
    return 0;
}

/** Empty / comment-only file produces 0 triangles. */
static int test_load_empty_file(void) {
    const char *obj = "# just a comment\n\n";
    const char *path = write_temp_obj(obj);
    ASSERT_TRUE(path != NULL);

    float verts[9];
    uint32_t tri_count = 0;
    int rc = obj_load_triangles(path, 1.0f, verts, 1, &tri_count);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(tri_count, 0);

    cleanup_temp(path);
    return 0;
}

/** NULL path returns error. */
static int test_null_path_returns_error(void) {
    float verts[9];
    uint32_t tri_count = 0;
    int rc = obj_load_triangles(NULL, 1.0f, verts, 1, &tri_count);
    ASSERT_TRUE(rc != 0);
    return 0;
}

/** Buffer too small returns error and reports needed count. */
static int test_buffer_too_small(void) {
    const char *obj =
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
        "f 1 2 3\nf 1 3 4\n";
    const char *path = write_temp_obj(obj);
    ASSERT_TRUE(path != NULL);

    float verts[9]; /* room for 1 tri only */
    uint32_t tri_count = 0;
    int rc = obj_load_triangles(path, 1.0f, verts, 1, &tri_count);
    /* Should return error because 2 tris > 1 capacity. */
    ASSERT_TRUE(rc != 0);
    /* tri_count should report actual count needed. */
    ASSERT_EQ(tri_count, 2);

    cleanup_temp(path);
    return 0;
}

/** Load the real armadillo OBJ and check triangle count. */
static int test_load_armadillo(void) {
    const char *path = "assets/test/armadillo.obj";
    /* First pass: query count with 0 capacity. */
    uint32_t tri_count = 0;
    int rc = obj_load_triangles(path, 1.0f, NULL, 0, &tri_count);
    ASSERT_TRUE(rc != 0); /* overflow expected */
    ASSERT_EQ(tri_count, 212574); /* known face count */

    /* Second pass: allocate and load. */
    float *verts = malloc((size_t)tri_count * 9 * sizeof(float));
    ASSERT_TRUE(verts != NULL);
    uint32_t loaded = 0;
    rc = obj_load_triangles(path, 1.0f, verts, tri_count, &loaded);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(loaded, 212574);

    /* Spot-check: no NaN or Inf in first triangle. */
    for (int i = 0; i < 9; i++) {
        ASSERT_TRUE(isfinite(verts[i]));
    }

    free(verts);
    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

#define RUN(fn)                                                               \
    do {                                                                       \
        printf("  %-50s ", #fn);                                               \
        int _rc = fn();                                                        \
        printf("%s\n", _rc == 0 ? "[OK]" : "[FAIL]");                          \
        if (_rc) fails++;                                                      \
        total++;                                                               \
    } while (0)

int main(void) {
    int fails = 0, total = 0;
    printf("RUN p110_obj_loader_tests\n");

    RUN(test_load_minimal_obj);
    RUN(test_load_vertex_normal_format);
    RUN(test_load_full_face_format);
    RUN(test_load_with_scale);
    RUN(test_load_empty_file);
    RUN(test_null_path_returns_error);
    RUN(test_buffer_too_small);
    RUN(test_load_armadillo);

    printf("\n%d/%d tests passed\n", total - fails, total);
    return fails ? 1 : 0;
}
