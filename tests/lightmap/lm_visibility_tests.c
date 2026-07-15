/**
 * @file lm_visibility_tests.c
 * @brief Unit tests for lm_visibility (SVO DDA occlusion / trace).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/lightmap/lm_visibility.h"
#include "ferrum/npc/npc_svo.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/physics/mesh_collider.h"

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                     \
    do {                                                                     \
        float _e = (exp), _a = (act);                                        \
        if (fabsf(_e - _a) > (eps)) {                                        \
            printf("  FAIL %s:%d: |%.4f - %.4f| > %.4f\n", __FILE__,         \
                   __LINE__, _e, _a, (float)(eps));                          \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }

/* Build an 8 m cube grid with a solid wall on the x = 4 plane, y,z in [2,6]. */
static bool build_wall_grid(npc_svo_grid_t *grid)
{
    phys_aabb_t bounds = { { 0.0f, 0.0f, 0.0f }, { 8.0f, 8.0f, 8.0f } };
    if (!npc_svo_grid_init(grid, bounds, 5)) /* voxel_size = 8/32 = 0.25 m */
        return false;
    phys_triangle_t t0 = { { { 4.0f, 2.0f, 2.0f },
                             { 4.0f, 6.0f, 2.0f },
                             { 4.0f, 6.0f, 6.0f } } };
    phys_triangle_t t1 = { { { 4.0f, 2.0f, 2.0f },
                             { 4.0f, 6.0f, 6.0f },
                             { 4.0f, 2.0f, 6.0f } } };
    npc_svo_rasterize_triangle(grid, &t0);
    npc_svo_rasterize_triangle(grid, &t1);
    return true;
}

/* Happy: a ray driven straight into the wall is occluded. */
static int test_occluded_hits_wall(void)
{
    npc_svo_grid_t grid;
    ASSERT_TRUE(build_wall_grid(&grid));
    bool occ = lm_visibility_occluded(&grid, v3(1, 4, 4), v3(1, 0, 0), 6.0f);
    npc_svo_grid_destroy(&grid);
    ASSERT_TRUE(occ);
    return 0;
}

/* Edge: a ray below the wall's y,z extent passes cleanly. */
static int test_not_occluded_past_extent(void)
{
    npc_svo_grid_t grid;
    ASSERT_TRUE(build_wall_grid(&grid));
    bool occ = lm_visibility_occluded(&grid, v3(1, 0.5f, 0.5f), v3(1, 0, 0), 6.0f);
    npc_svo_grid_destroy(&grid);
    ASSERT_TRUE(!occ);
    return 0;
}

/* Happy: trace reports the hit at the wall plane with a -x normal. */
static int test_trace_reports_hit(void)
{
    npc_svo_grid_t grid;
    ASSERT_TRUE(build_wall_grid(&grid));
    lm_ray_hit_t h;
    bool hit = lm_visibility_trace(&grid, v3(1, 4, 4), v3(1, 0, 0), 0.0f, 6.0f, &h);
    npc_svo_grid_destroy(&grid);
    ASSERT_TRUE(hit && h.hit);
    ASSERT_FLOAT_NEAR(4.0f, h.position.x, 0.3f);   /* within a voxel of the wall */
    ASSERT_FLOAT_NEAR(-1.0f, h.normal.x, 1e-3f);
    ASSERT_FLOAT_NEAR(0.0f, h.normal.y, 1e-3f);
    ASSERT_FLOAT_NEAR(0.0f, h.normal.z, 1e-3f);
    return 0;
}

/* Edge: mutual visibility across / around the wall. */
static int test_segment_visibility(void)
{
    npc_svo_grid_t grid;
    ASSERT_TRUE(build_wall_grid(&grid));
    bool blocked = lm_visibility_segment(&grid, v3(1, 4, 4), v3(7, 4, 4));
    bool clear = lm_visibility_segment(&grid, v3(1, 0.5f, 0.5f), v3(7, 0.5f, 0.5f));
    npc_svo_grid_destroy(&grid);
    ASSERT_TRUE(!blocked);   /* wall between -> not visible */
    ASSERT_TRUE(clear);      /* below the wall -> visible */
    return 0;
}

/* Edge: a zero-length segment is trivially visible. */
static int test_zero_length_segment(void)
{
    npc_svo_grid_t grid;
    ASSERT_TRUE(build_wall_grid(&grid));
    bool vis = lm_visibility_segment(&grid, v3(3, 3, 3), v3(3, 3, 3));
    npc_svo_grid_destroy(&grid);
    ASSERT_TRUE(vis);
    return 0;
}

/* Fail-safe: a zero direction and an origin outside the grid do not crash and
 * report not-occluded / no-hit. */
static int test_degenerate_inputs(void)
{
    npc_svo_grid_t grid;
    ASSERT_TRUE(build_wall_grid(&grid));
    ASSERT_TRUE(!lm_visibility_occluded(&grid, v3(1, 4, 4), v3(0, 0, 0), 6.0f));
    lm_ray_hit_t h;
    bool hit = lm_visibility_trace(&grid, v3(-50, -50, -50), v3(-1, 0, 0), 0.0f, 6.0f, &h);
    npc_svo_grid_destroy(&grid);
    ASSERT_TRUE(!hit && !h.hit);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "occluded_hits_wall", test_occluded_hits_wall },
    { "not_occluded_past_extent", test_not_occluded_past_extent },
    { "trace_reports_hit", test_trace_reports_hit },
    { "segment_visibility", test_segment_visibility },
    { "zero_length_segment", test_zero_length_segment },
    { "degenerate_inputs", test_degenerate_inputs },
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
