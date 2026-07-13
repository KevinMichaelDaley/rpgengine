/**
 * @file lm_mesh_luxel_tests.c
 * @brief Unit tests for triangle lightmap-UV luxelization.
 */
#include <math.h>
#include <string.h>
#include <stdio.h>

#include "ferrum/lightmap/lm_mesh_luxel.h"
#include "ferrum/lightmap/lm_sh.h"

#define ASSERT_TRUE(c)                                                        \
    do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

/* A unit quad in the z=0 plane, normal +z, uv1 covering [0,1]. */
static const float QPOS[] = { 0,0,0,  1,0,0,  1,1,0,  0,1,0 };
static const float QNRM[] = { 0,0,1,  0,0,1,  0,0,1,  0,0,1 };
static const float QUV1[] = { 0,0,    1,0,    1,1,    0,1 };
static const uint32_t QIDX[] = { 0,1,2,  0,2,3 };

static lm_mesh_t quad_mesh(void) {
    lm_mesh_t m; memset(&m, 0, sizeof(m));
    m.positions = QPOS; m.normals = QNRM; m.uv0 = NULL; m.uv1 = QUV1; m.indices = QIDX;
    m.vert_count = 4; m.index_count = 6;
    m.albedo_image = NULL; m.emissive_image = NULL;
    m.albedo = (vec3_t){ 0.7f, 0.7f, 0.7f }; m.emissive = (vec3_t){ 0, 0, 0 };
    m.material = 0; m.lightmap_resolution = 8;
    return m;
}

/* The quad fills an 8x8 rect: 64 luxels, correct plane + normal, atlas coords. */
static int test_full_quad(void) {
    lm_mesh_t m = quad_mesh();
    lm_atlas_rect_t rect = { 8, 8, 0, 0 };
    static lm_luxel_t lux[64]; static uint32_t ax[64], ay[64]; static uint8_t vis[64];
    uint32_t n = lm_mesh_luxelize(&m, &rect, 8, 8, lux, ax, ay, vis);
    ASSERT_TRUE(n == 64);
    for (uint32_t i = 0; i < n; ++i) {
        ASSERT_TRUE(fabsf(lux[i].pos.z) < 1e-5f);           /* on the quad plane */
        ASSERT_TRUE(lux[i].pos.x >= -0.01f && lux[i].pos.x <= 1.01f);
        ASSERT_TRUE(lux[i].pos.y >= -0.01f && lux[i].pos.y <= 1.01f);
        ASSERT_TRUE(fabsf(lux[i].normal.z - 1.0f) < 1e-4f); /* +z normal */
        ASSERT_TRUE(ax[i] < 8 && ay[i] < 8);
    }
    return 0;
}

/* Placed at a non-zero atlas rect, atlas coords are offset accordingly. */
static int test_offset_rect(void) {
    lm_mesh_t m = quad_mesh(); m.lightmap_resolution = 4;
    lm_atlas_rect_t rect = { 4, 4, 10, 20 };
    static lm_luxel_t lux[16]; static uint32_t ax[16], ay[16]; static uint8_t vis[16];
    uint32_t n = lm_mesh_luxelize(&m, &rect, 64, 64, lux, ax, ay, vis);
    ASSERT_TRUE(n == 16);
    for (uint32_t i = 0; i < n; ++i) {
        ASSERT_TRUE(ax[i] >= 10 && ax[i] < 14);
        ASSERT_TRUE(ay[i] >= 20 && ay[i] < 24);
    }
    return 0;
}

/* A half-covering triangle (only one of the two) covers ~half the rect. */
static int test_partial_coverage(void) {
    lm_mesh_t m = quad_mesh();
    m.index_count = 3; /* only triangle 0,1,2 (lower-right half) */
    lm_atlas_rect_t rect = { 16, 16, 0, 0 };
    static lm_luxel_t lux[256]; static uint32_t ax[256], ay[256]; static uint8_t vis[256];
    uint32_t n = lm_mesh_luxelize(&m, &rect, 16, 16, lux, ax, ay, vis);
    ASSERT_TRUE(n > 90 && n < 170); /* roughly half of 256 */
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "full_quad", test_full_quad },
    { "offset_rect", test_offset_rect },
    { "partial_coverage", test_partial_coverage },
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
