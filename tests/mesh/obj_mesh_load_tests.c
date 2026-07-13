/**
 * @file obj_mesh_load_tests.c
 * @brief Unit tests for the indexed OBJ loader (positions/normals/uvs).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/mesh/obj_loader.h"

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "ASSERT failed %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static const char *QUAD_WITH_N =
    "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
    "vn 0 0 1\n"
    "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n";

static const char *QUAD_NO_N =
    "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
    "f 1/1 2/2 3/3\nf 1/1 3/3 4/4\n";

static int write_tmp(const char *path, const char *text) {
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    fputs(text, f); fclose(f); return 0;
}

/* A quad with explicit normals: 4 unique verts, 6 indices, correct uv/normal. */
static int test_indexed_with_normals(void) {
    const char *p = "/tmp/_obj_test_n.obj";
    ASSERT_TRUE(write_tmp(p, QUAD_WITH_N) == 0);
    obj_mesh_t m;
    ASSERT_TRUE(obj_mesh_load(p, 1.0f, &m) == 0);
    ASSERT_TRUE(m.vert_count == 4);
    ASSERT_TRUE(m.index_count == 6);
    /* every normal is +z. */
    for (uint32_t v = 0; v < m.vert_count; ++v) {
        ASSERT_TRUE(fabsf(m.normals[v*3+2] - 1.0f) < 1e-5f);
        ASSERT_TRUE(fabsf(m.normals[v*3+0]) < 1e-5f);
    }
    /* vertex 0 uv is (0,0); some vertex has uv (1,0). */
    ASSERT_TRUE(m.uvs[0] == 0.0f && m.uvs[1] == 0.0f);
    int found_10 = 0;
    for (uint32_t v = 0; v < m.vert_count; ++v)
        if (fabsf(m.uvs[v*2]-1.0f)<1e-5f && fabsf(m.uvs[v*2+1])<1e-5f) found_10 = 1;
    ASSERT_TRUE(found_10);
    obj_mesh_free(&m);
    ASSERT_TRUE(m.positions == NULL && m.vert_count == 0);
    return 0;
}

/* Without vn lines, smooth normals are generated (~ +z for a flat quad). */
static int test_generated_normals(void) {
    const char *p = "/tmp/_obj_test_nn.obj";
    ASSERT_TRUE(write_tmp(p, QUAD_NO_N) == 0);
    obj_mesh_t m;
    ASSERT_TRUE(obj_mesh_load(p, 1.0f, &m) == 0);
    ASSERT_TRUE(m.vert_count == 4 && m.index_count == 6);
    for (uint32_t v = 0; v < m.vert_count; ++v) {
        float nz = m.normals[v*3+2];
        ASSERT_TRUE(nz > 0.9f); /* generated normal points +z */
    }
    obj_mesh_free(&m);
    return 0;
}

/* Missing file / bad args fail cleanly. */
static int test_errors(void) {
    obj_mesh_t m;
    ASSERT_TRUE(obj_mesh_load("/no/such/file.obj", 1.0f, &m) == -1);
    ASSERT_TRUE(obj_mesh_load(NULL, 1.0f, &m) == -1);
    obj_mesh_free(NULL);
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "indexed_with_normals", test_indexed_with_normals },
    { "generated_normals", test_generated_normals },
    { "errors", test_errors },
};

int main(void) {
    int failed = 0;
    for (size_t i = 0; i < sizeof(TESTS)/sizeof(TESTS[0]); ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int r = TESTS[i].fn();
        printf(r == 0 ? "OK   %s\n" : "FAIL %s\n", TESTS[i].name);
        failed += (r != 0);
    }
    printf("%s (%d failed)\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}
