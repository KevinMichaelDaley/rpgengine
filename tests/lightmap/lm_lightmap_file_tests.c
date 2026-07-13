/**
 * @file lm_lightmap_file_tests.c
 * @brief Roundtrip test for lightmap serialization.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/lm_lightmap_file.h"
#include "ferrum/lightmap/lm_mesh_bake.h"
#include "ferrum/memory/arena.h"

#define ASSERT_TRUE(c)                                                        \
    do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static const float FPOS[] = { 0,0,0,  4,0,0,  4,0,4,  0,0,4 };
static const float FNRM[] = { 0,1,0,  0,1,0,  0,1,0,  0,1,0 };
static const float FUV[]  = { 0,0,    1,0,    1,1,    0,1 };
static const uint32_t FIDX[] = { 0,1,2,  0,2,3 };

static int test_roundtrip(void) {
    static char buf[16 * 1024 * 1024];
    arena_t arena; arena_init(&arena, buf, sizeof(buf));
    lm_mesh_t floor; memset(&floor, 0, sizeof floor);
    floor.positions=FPOS; floor.normals=FNRM; floor.uv0=FUV; floor.uv1=FUV; floor.indices=FIDX;
    floor.vert_count=4; floor.index_count=6;
    floor.albedo=(vec3_t){0.7f,0.7f,0.7f}; floor.emissive=(vec3_t){0,0,0};
    floor.material=0; floor.lightmap_resolution=16;
    lm_light_t sun; memset(&sun,0,sizeof sun);
    sun.kind=LM_LIGHT_DIRECTIONAL; sun.direction=(vec3_t){0,-1,0}; sun.color=(vec3_t){3,3,3};
    lm_material_t fb={{0,0,0},{0,0,0}};
    lm_mesh_scene_t scene={ &floor, 1, &sun, 1, { NULL, 0, fb } };
    lm_bake_config_t cfg={0};
    cfg.svo_bounds=(phys_aabb_t){{-1,-1,-1},{5,5,5}}; cfg.svo_depth=5;
    cfg.atlas_width=64; cfg.atlas_padding=1; cfg.solve.near_radius=10; cfg.solve.max_shots=100;
    cfg.solve.residual_epsilon=1e-3f;
    lm_mesh_bake_result_t res;
    ASSERT_TRUE(lm_mesh_bake(&scene,&cfg,&res,&arena));

    /* Reference coefficient-0 image from the bake. */
    uint32_t px = res.atlas.width * res.atlas.height;
    float *ref = malloc((size_t)px*3*sizeof(float));
    lm_mesh_bake_readback_sh(&res, 0, ref);

    const char *path = "/tmp/_lm_roundtrip.flm";
    ASSERT_TRUE(lm_lightmap_save(&res, path));

    lm_lightmap_data_t data;
    ASSERT_TRUE(lm_lightmap_load(path, &data));
    ASSERT_TRUE(data.atlas_w == res.atlas.width && data.atlas_h == res.atlas.height);
    ASSERT_TRUE(data.n_meshes == 1);
    ASSERT_TRUE(data.rects[0].w == 16 && data.rects[0].h == 16);
    /* Coefficient-0 image roundtrips bit-for-bit. */
    for (uint32_t i = 0; i < px*3; ++i)
        ASSERT_TRUE(data.coeffs[0][i] == ref[i]);
    /* Some coefficient carries baked energy. */
    float mx=0; for (uint32_t i=0;i<px*3;++i) if (fabsf(data.coeffs[0][i])>mx) mx=fabsf(data.coeffs[0][i]);
    ASSERT_TRUE(mx > 0.0f);

    lm_lightmap_data_free(&data);
    ASSERT_TRUE(data.coeffs[0] == NULL && data.n_meshes == 0);
    free(ref);
    return 0;
}

/* Bad magic / missing file fail cleanly. */
static int test_errors(void) {
    lm_lightmap_data_t d;
    ASSERT_TRUE(!lm_lightmap_load("/no/such/lm.flm", &d));
    FILE *f = fopen("/tmp/_lm_bad.flm", "wb"); fputs("XXXX....", f); fclose(f);
    ASSERT_TRUE(!lm_lightmap_load("/tmp/_lm_bad.flm", &d));
    lm_lightmap_data_free(NULL);
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "roundtrip", test_roundtrip },
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
