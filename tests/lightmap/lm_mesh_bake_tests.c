/**
 * @file lm_mesh_bake_tests.c
 * @brief Unit tests for the triangle-mesh lightmap bake.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/lm_mesh_bake.h"
#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/memory/arena.h"

#define ASSERT_TRUE(c)                                                        \
    do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

/* A 4x4 floor quad in the y=0 plane, normal +y, uv1 over [0,1]. */
static const float FPOS[] = { 0,0,0,  4,0,0,  4,0,4,  0,0,4 };
static const float FNRM[] = { 0,1,0,  0,1,0,  0,1,0,  0,1,0 };
static const float FUV1[] = { 0,0,    1,0,    1,1,    0,1 };
static const uint32_t FIDX[] = { 0,1,2,  0,2,3 };

static float centre_irradiance(lm_mesh_bake_result_t *r) {
    /* pick the luxel nearest the floor centre (2,0,2). */
    float best = 1e30f; uint32_t bi = 0;
    for (uint32_t i = 0; i < r->n_luxels; ++i) {
        float dx = r->combined.luxels[i].pos.x - 2.0f;
        float dz = r->combined.luxels[i].pos.z - 2.0f;
        float d = dx*dx + dz*dz;
        if (d < best) { best = d; bi = i; }
    }
    lm_luxel_t *lx = &r->combined.luxels[bi];
    return lm_sh9_irradiance(&lx->sh[0], lx->normal);
}

static int test_directional_bake(void) {
    static char buf[16 * 1024 * 1024];
    arena_t arena; arena_init(&arena, buf, sizeof(buf));

    lm_mesh_t floor;
    floor.positions=FPOS; floor.normals=FNRM; floor.uv1=FUV1; floor.indices=FIDX;
    floor.vert_count=4; floor.index_count=6;
    floor.albedo=(vec3_t){0.7f,0.7f,0.7f}; floor.emissive=(vec3_t){0,0,0};
    floor.material=0; floor.lightmap_resolution=16;

    lm_light_t sun; memset(&sun,0,sizeof sun);
    sun.kind=LM_LIGHT_DIRECTIONAL; sun.direction=(vec3_t){0,-1,0}; /* points down */
    sun.color=(vec3_t){3,3,3};

    lm_material_t fb={{0,0,0},{0,0,0}};
    lm_mesh_scene_t scene={ &floor, 1, &sun, 1, { NULL, 0, fb } };
    lm_bake_config_t cfg={0};
    cfg.svo_bounds=(phys_aabb_t){{-1,-1,-1},{5,5,5}};
    cfg.svo_depth=5; cfg.atlas_width=128; cfg.atlas_padding=2;
    cfg.farfield_samples=0;
    cfg.solve.near_radius=10.0f; cfg.solve.max_shots=200; cfg.solve.residual_epsilon=1e-3f;
    cfg.seed=1u;

    lm_mesh_bake_result_t res;
    ASSERT_TRUE(lm_mesh_bake(&scene,&cfg,&res,&arena));
    ASSERT_TRUE(res.n_luxels >= 200 && res.n_luxels <= 256);
    /* Directional sun straight down on a +y floor -> irradiance ~ color = 3. */
    float e = centre_irradiance(&res);
    ASSERT_TRUE(e > 1.5f);
    /* atlas coords in range. */
    for (uint32_t i=0;i<res.n_luxels;++i)
        ASSERT_TRUE(res.atlas_x[i] < res.atlas.width && res.atlas_y[i] < res.atlas.height);
    return 0;
}

/* Readback fills the atlas at covered texels and clears the rest. */
static int test_readback(void) {
    static char buf[16 * 1024 * 1024];
    arena_t arena; arena_init(&arena, buf, sizeof(buf));
    lm_mesh_t floor;
    floor.positions=FPOS; floor.normals=FNRM; floor.uv1=FUV1; floor.indices=FIDX;
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
    uint32_t px = res.atlas.width*res.atlas.height;
    float *img = malloc((size_t)px*3*sizeof(float));
    lm_mesh_bake_readback_sh(&res, 0, img);
    float mx=0; for (uint32_t i=0;i<px*3;++i) if (img[i]>mx) mx=img[i];
    ASSERT_TRUE(mx > 0.0f); /* some texel has a non-zero DC coefficient */
    free(img);
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "directional_bake", test_directional_bake },
    { "readback", test_readback },
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
