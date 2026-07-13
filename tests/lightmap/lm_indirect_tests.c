/**
 * @file lm_indirect_tests.c
 * @brief Unit tests for lm_indirect (analytic-light direct irradiance seed).
 */
#include <math.h>
#include <stdio.h>

#include "ferrum/lightmap/lm_indirect.h"
#include "ferrum/lightmap/lm_light.h"
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

static lm_surface_t make_floor(void)
{
    lm_surface_t s;
    s.origin = v3(0, 0, 0);
    s.edge_u = v3(4, 0, 0);
    s.edge_v = v3(0, 4, 0);
    s.normal = v3(0, 0, 1);
    s.albedo = v3(0.5f, 0.5f, 0.5f);
    s.emissive = v3(0, 0, 0);
    s.res_u = 4;
    s.res_v = 4;
    return s;
}

/* Centre-luxel irradiance magnitude (channel 0) from the RGB seed buffer. */
static float centre_r(const lm_lightmap_t *lm, const float *irr)
{
    (void)lm;
    uint32_t idx = 2 * 4 + 2; /* iv*res_u + iu at (2,2) */
    return irr[idx * 3 + 0];
}

/* Happy: a point light above the floor seeds positive irradiance. */
static int test_point_lit(void)
{
    static char buf[1 << 16];
    arena_t arena;
    arena_init(&arena, buf, sizeof(buf));
    lm_surface_t floor = make_floor();
    lm_lightmap_t lm;
    ASSERT_TRUE(lm_lightmap_from_surface(&lm, &floor, &arena));

    lm_light_t light;
    light.kind = LM_LIGHT_POINT;
    light.position = v3(2, 2, 3);
    light.direction = v3(0, 0, -1);
    light.color = v3(10, 10, 10);
    light.range = 50.0f;
    light.cos_inner = light.cos_outer = 0.0f;

    static float irr[4 * 4 * 3];
    lm_indirect_direct_irradiance(&lm, &light, 1, NULL, irr);
    ASSERT_TRUE(centre_r(&lm, irr) > 0.0f);
    return 0;
}

/* Shadow: an occluder between the light and the floor zeros the seed. */
static int test_point_shadowed(void)
{
    static char buf[1 << 16];
    arena_t arena;
    arena_init(&arena, buf, sizeof(buf));
    lm_surface_t floor = make_floor();
    lm_lightmap_t lm;
    ASSERT_TRUE(lm_lightmap_from_surface(&lm, &floor, &arena));

    lm_light_t light;
    light.kind = LM_LIGHT_POINT;
    light.position = v3(2, 2, 3);
    light.direction = v3(0, 0, -1);
    light.color = v3(10, 10, 10);
    light.range = 50.0f;
    light.cos_inner = light.cos_outer = 0.0f;

    npc_svo_grid_t svo;
    phys_aabb_t bounds = { { -2, -2, -2 }, { 6, 6, 6 } };
    ASSERT_TRUE(npc_svo_grid_init(&svo, bounds, 5));
    phys_triangle_t t0 = { { { -1, -1, 1.5f }, { 5, -1, 1.5f }, { 5, 5, 1.5f } } };
    phys_triangle_t t1 = { { { -1, -1, 1.5f }, { 5, 5, 1.5f }, { -1, 5, 1.5f } } };
    npc_svo_rasterize_triangle(&svo, &t0);
    npc_svo_rasterize_triangle(&svo, &t1);

    static float irr[4 * 4 * 3];
    lm_indirect_direct_irradiance(&lm, &light, 1, &svo, irr);
    ASSERT_TRUE(centre_r(&lm, irr) <= 1e-6f);

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* Directional light straight down lights every luxel equally. */
static int test_directional(void)
{
    static char buf[1 << 16];
    arena_t arena;
    arena_init(&arena, buf, sizeof(buf));
    lm_surface_t floor = make_floor();
    lm_lightmap_t lm;
    ASSERT_TRUE(lm_lightmap_from_surface(&lm, &floor, &arena));

    lm_light_t light;
    light.kind = LM_LIGHT_DIRECTIONAL;
    light.position = v3(0, 0, 0);
    light.direction = v3(0, 0, -1); /* travels downward, faces the floor */
    light.color = v3(3, 3, 3);
    light.range = 0.0f;
    light.cos_inner = light.cos_outer = 0.0f;

    static float irr[4 * 4 * 3];
    lm_indirect_direct_irradiance(&lm, &light, 1, NULL, irr);
    float a = centre_r(&lm, irr);
    ASSERT_TRUE(a > 0.0f);
    /* Uniform: a corner luxel matches the centre. */
    ASSERT_TRUE(fabsf(irr[0] - a) < 1e-5f);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "point_lit", test_point_lit },
    { "point_shadowed", test_point_shadowed },
    { "directional", test_directional },
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
