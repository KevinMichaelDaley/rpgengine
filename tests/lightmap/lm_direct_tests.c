/**
 * @file lm_direct_tests.c
 * @brief Unit tests for lm_direct (area-light direct lighting + SVO shadows).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/lightmap/lm_direct.h"
#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/math/vec3.h"
#include "ferrum/memory/arena.h"
#include "ferrum/npc/npc_svo.h"

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }

/* A 4x4 floor in the z=0 plane spanning [0,4]^2, facing +z (or flip_normal). */
static lm_surface_t make_floor(int flip_normal)
{
    lm_surface_t s;
    s.origin = v3(0, 0, 0);
    s.edge_u = v3(4, 0, 0);
    s.edge_v = v3(0, 4, 0);
    s.normal = v3(0, 0, flip_normal ? -1.0f : 1.0f);
    s.albedo = v3(0.5f, 0.5f, 0.5f);
    s.emissive = v3(0, 0, 0);
    s.res_u = 4;
    s.res_v = 4;
    return s;
}

/* A downward-facing emitter quad centred above the floor at z=4. */
static lm_surface_t make_emitter(void)
{
    lm_surface_t e;
    e.origin = v3(1, 1, 4);
    e.edge_u = v3(2, 0, 0);
    e.edge_v = v3(0, 2, 0);
    e.normal = v3(0, 0, -1);
    e.albedo = v3(0, 0, 0);
    e.emissive = v3(5, 5, 5);
    e.res_u = 1;
    e.res_v = 1;
    return e;
}

/* Irradiance seen by the centre luxel (2,2) for channel 0. */
static float centre_irradiance(lm_lightmap_t *lm)
{
    lm_luxel_t *luxel = lm_lightmap_at(lm, 2, 2);
    return lm_sh9_irradiance(&luxel->sh[0], luxel->normal);
}

/* Happy: a lit floor under an emitter, no occluder, gets positive irradiance. */
static int test_direct_lit(void)
{
    static char buf[1 << 16];
    arena_t arena;
    arena_init(&arena, buf, sizeof(buf));
    lm_surface_t floor = make_floor(0);
    lm_lightmap_t lm;
    ASSERT_TRUE(lm_lightmap_from_surface(&lm, &floor, &arena));
    lm_surface_t emitter = make_emitter();
    lm_direct_bake(&lm, &emitter, 1, NULL, 64, 1234u);
    ASSERT_TRUE(centre_irradiance(&lm) > 0.0f);
    return 0;
}

/* Shadow: an occluder plane between floor and emitter kills the light. */
static int test_direct_shadowed(void)
{
    static char buf[1 << 16];
    arena_t arena;
    arena_init(&arena, buf, sizeof(buf));
    lm_surface_t floor = make_floor(0);
    lm_lightmap_t lit, shad;
    ASSERT_TRUE(lm_lightmap_from_surface(&lit, &floor, &arena));
    ASSERT_TRUE(lm_lightmap_from_surface(&shad, &floor, &arena));
    lm_surface_t emitter = make_emitter();

    /* SVO with a solid horizontal plane at z=2 covering the sight lines. */
    npc_svo_grid_t svo;
    phys_aabb_t bounds = { { -2, -2, -2 }, { 6, 6, 6 } };
    ASSERT_TRUE(npc_svo_grid_init(&svo, bounds, 5));
    phys_triangle_t t0 = { { { -1, -1, 2 }, { 5, -1, 2 }, { 5, 5, 2 } } };
    phys_triangle_t t1 = { { { -1, -1, 2 }, { 5, 5, 2 }, { -1, 5, 2 } } };
    npc_svo_rasterize_triangle(&svo, &t0);
    npc_svo_rasterize_triangle(&svo, &t1);

    lm_direct_bake(&lit, &emitter, 1, NULL, 64, 7u);
    lm_direct_bake(&shad, &emitter, 1, &svo, 64, 7u);
    float lit_e = centre_irradiance(&lit);
    float shad_e = centre_irradiance(&shad);
    ASSERT_TRUE(lit_e > 0.0f);
    ASSERT_TRUE(shad_e < 0.05f * lit_e); /* occluded -> essentially dark */

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* Edge: a luxel facing away from the emitter receives (near) nothing. */
static int test_direct_backface(void)
{
    static char buf[1 << 16];
    arena_t arena;
    arena_init(&arena, buf, sizeof(buf));
    lm_surface_t floor = make_floor(1); /* normal -z, away from emitter above */
    lm_lightmap_t lm;
    ASSERT_TRUE(lm_lightmap_from_surface(&lm, &floor, &arena));
    lm_surface_t emitter = make_emitter();
    lm_direct_bake(&lm, &emitter, 1, NULL, 64, 99u);
    ASSERT_TRUE(centre_irradiance(&lm) <= 1e-5f);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "direct_lit", test_direct_lit },
    { "direct_shadowed", test_direct_shadowed },
    { "direct_backface", test_direct_backface },
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
