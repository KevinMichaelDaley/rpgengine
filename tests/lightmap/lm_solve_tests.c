/**
 * @file lm_solve_tests.c
 * @brief Unit tests for lm_solve (progressive radiosity shooting).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_solve.h"
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

/* Two unit patches facing each other along x, 2 units apart. Luxel 0 (the
 * shooter) has albedo 1; luxel 1 (the receiver) albedo 0.5. */
static void build_pair(lm_luxel_t luxels[2], lm_lightmap_t *lm, vec3_t pos[2])
{
    memset(luxels, 0, 2 * sizeof(lm_luxel_t));
    luxels[0].pos = v3(0, 0, 0);
    luxels[0].normal = v3(1, 0, 0);
    luxels[0].albedo = v3(1, 1, 1);
    luxels[1].pos = v3(2, 0, 0);
    luxels[1].normal = v3(-1, 0, 0);
    luxels[1].albedo = v3(0.5f, 0.5f, 0.5f);
    for (int i = 0; i < 2; ++i) {
        for (int c = 0; c < 3; ++c)
            lm_sh9_zero(&luxels[i].sh[c]);
        pos[i] = luxels[i].pos;
    }
    lm->luxels = luxels;
    lm->res_u = 2;
    lm->res_v = 1;
}

static float recv_irradiance(lm_lightmap_t *lm)
{
    lm_luxel_t *b = &lm->luxels[1];
    return lm_sh9_irradiance(&b->sh[0], b->normal);
}

/* Happy: shooter with residual bleeds light onto the facing receiver. */
static int test_bleed_lit(void)
{
    static char buf[1 << 16];
    arena_t arena;
    arena_init(&arena, buf, sizeof(buf));

    lm_luxel_t luxels[2];
    lm_lightmap_t lm;
    vec3_t pos[2];
    build_pair(luxels, &lm, pos);

    lm_kdtree_t kd;
    ASSERT_TRUE(lm_kdtree_build(&kd, pos, 2, &arena));

    float seed[6] = { 10, 10, 10, 0, 0, 0 }; /* only the shooter is lit */
    lm_solver_t solver;
    ASSERT_TRUE(lm_solver_init(&solver, &lm, &kd, NULL, seed, 1.0f, &arena));

    lm_solve_params_t p = { 0 };
    p.near_radius = 10.0f;
    p.max_shots = 100;
    p.residual_epsilon = 1e-4f;
    p.use_region = false;

    uint32_t shots = lm_solver_run(&solver, &p);
    ASSERT_TRUE(shots >= 1);
    ASSERT_TRUE(recv_irradiance(&lm) > 0.0f);
    return 0;
}

/* Shadow: an occluder plane between the patches blocks the bleed. */
static int test_bleed_occluded(void)
{
    static char buf[1 << 16];
    arena_t arena;
    arena_init(&arena, buf, sizeof(buf));

    lm_luxel_t luxels[2];
    lm_lightmap_t lm;
    vec3_t pos[2];
    build_pair(luxels, &lm, pos);

    lm_kdtree_t kd;
    ASSERT_TRUE(lm_kdtree_build(&kd, pos, 2, &arena));

    npc_svo_grid_t svo;
    phys_aabb_t bounds = { { -2, -2, -2 }, { 4, 2, 2 } };
    ASSERT_TRUE(npc_svo_grid_init(&svo, bounds, 5));
    phys_triangle_t t0 = { { { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } } };
    phys_triangle_t t1 = { { { 1, -1, -1 }, { 1, 1, 1 }, { 1, -1, 1 } } };
    npc_svo_rasterize_triangle(&svo, &t0);
    npc_svo_rasterize_triangle(&svo, &t1);

    float seed[6] = { 10, 10, 10, 0, 0, 0 };
    lm_solver_t solver;
    ASSERT_TRUE(lm_solver_init(&solver, &lm, &kd, &svo, seed, 1.0f, &arena));

    lm_solve_params_t p = { 0 };
    p.near_radius = 10.0f;
    p.max_shots = 100;
    p.residual_epsilon = 1e-4f;

    lm_solver_run(&solver, &p);
    ASSERT_TRUE(recv_irradiance(&lm) <= 1e-6f);

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* Partial bake: a region excluding the receiver keeps it static (dark). */
static int test_region_gate(void)
{
    static char buf[1 << 16];
    arena_t arena;
    arena_init(&arena, buf, sizeof(buf));

    lm_luxel_t luxels[2];
    lm_lightmap_t lm;
    vec3_t pos[2];
    build_pair(luxels, &lm, pos);

    lm_kdtree_t kd;
    ASSERT_TRUE(lm_kdtree_build(&kd, pos, 2, &arena));

    float seed[6] = { 10, 10, 10, 0, 0, 0 };
    lm_solver_t solver;
    ASSERT_TRUE(lm_solver_init(&solver, &lm, &kd, NULL, seed, 1.0f, &arena));

    lm_solve_params_t p = { 0 };
    p.near_radius = 10.0f;
    p.max_shots = 100;
    p.residual_epsilon = 1e-4f;
    p.use_region = true;
    /* Region around the shooter only; the receiver at x=2 is outside. */
    p.bake_region.min = v3(-0.5f, -1, -1);
    p.bake_region.max = v3(0.5f, 1, 1);

    lm_solver_run(&solver, &p);
    ASSERT_TRUE(recv_irradiance(&lm) <= 1e-6f);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "bleed_lit", test_bleed_lit },
    { "bleed_occluded", test_bleed_occluded },
    { "region_gate", test_region_gate },
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
