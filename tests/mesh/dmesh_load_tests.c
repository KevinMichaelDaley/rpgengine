/**
 * @file dmesh_load_tests.c
 * @brief Unit tests for the dual-UV binary mesh loader (dmesh_load).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/mesh/dmesh_loader.h"

#define ASSERT_TRUE(c)                                                        \
    do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-3f)

/* Write a 2-triangle quad as a triangle soup (6 corners, 4 distinct verts). */
static const char *write_quad(void)
{
    /* z=0 plane; normal +Z; uv0 = uv1 = xy. Verts A(0,0) B(1,0) C(1,1) D(0,1). */
    float A[10] = { 0,0,0, 0,0,1, 0,0, 0,0 };
    float B[10] = { 1,0,0, 0,0,1, 1,0, 1,0 };
    float C[10] = { 1,1,0, 0,0,1, 1,1, 1,1 };
    float D[10] = { 0,1,0, 0,0,1, 0,1, 0,1 };
    const float *soup[6] = { A, B, C, A, C, D }; /* ABC, ACD */
    const char *path = "/tmp/_dmesh_quad.dmesh";
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    uint32_t corners = 6;
    fwrite(&corners, sizeof(uint32_t), 1, f);
    for (int i = 0; i < 6; ++i) fwrite(soup[i], sizeof(float), 10, f);
    fclose(f);
    return path;
}

/* The soup's 6 corners weld to 4 unique verts + a 6-index buffer. */
static int test_dedup(void)
{
    const char *path = write_quad();
    ASSERT_TRUE(path != NULL);
    obj_mesh_t m;
    ASSERT_TRUE(dmesh_load(path, &m) == 0);
    ASSERT_TRUE(m.vert_count == 4);
    ASSERT_TRUE(m.index_count == 6);
    /* Both triangles reference shared verts A and C. */
    ASSERT_TRUE(m.indices[0] == m.indices[3]); /* A == A */
    ASSERT_TRUE(m.indices[2] == m.indices[4]); /* C == C */
    obj_mesh_free(&m);
    return 0;
}

/* Both UV channels round-trip; tangents are unit-length and in-plane (~+X). */
static int test_uvs_and_tangents(void)
{
    const char *path = write_quad();
    ASSERT_TRUE(path != NULL);
    obj_mesh_t m;
    ASSERT_TRUE(dmesh_load(path, &m) == 0);
    ASSERT_TRUE(m.uvs != NULL && m.uvs1 != NULL && m.tangents != NULL);
    for (uint32_t v = 0; v < m.vert_count; ++v) {
        /* uv0 == uv1 == xy for this mesh. */
        ASSERT_TRUE(NEAR(m.uvs[v*2], m.uvs1[v*2]) &&
                    NEAR(m.uvs[v*2+1], m.uvs1[v*2+1]));
        const float *t = &m.tangents[v*4];
        float len = sqrtf(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
        ASSERT_TRUE(NEAR(len, 1.0f));         /* unit tangent */
        ASSERT_TRUE(NEAR(t[0], 1.0f) && NEAR(t[2], 0.0f)); /* +X, in z=0 plane */
        ASSERT_TRUE(NEAR(fabsf(t[3]), 1.0f)); /* handedness +/-1 */
    }
    obj_mesh_free(&m);
    return 0;
}

/* Bad args / missing file fail cleanly. */
static int test_errors(void)
{
    obj_mesh_t m;
    ASSERT_TRUE(dmesh_load(NULL, &m) == -1);
    ASSERT_TRUE(dmesh_load("/no/such/file.dmesh", &m) == -1);
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "dedup", test_dedup },
    { "uvs_and_tangents", test_uvs_and_tangents },
    { "errors", test_errors },
};

int main(void)
{
    int failed = 0;
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int r = TESTS[i].fn();
        printf(r == 0 ? "OK   %s\n" : "FAIL %s\n", TESTS[i].name);
        failed += (r != 0);
    }
    printf("%s (%d failed)\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}
