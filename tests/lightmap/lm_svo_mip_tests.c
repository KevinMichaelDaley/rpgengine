/**
 * @file lm_svo_mip_tests.c
 * @brief Unit tests for the SVO shading mip pyramid (lm_svo_mip).
 */
#include <math.h>
#include <stdio.h>

#include "ferrum/lightmap/lm_svo_material.h"
#include "ferrum/lightmap/lm_svo_mip.h"
#include "ferrum/npc/npc_svo.h"
#include "ferrum/physics/mesh_collider.h"

#define ASSERT_TRUE(c)                                                        \
    do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-3f)

/* Stamp a single solid voxel of material id `mat` at world position p. */
static void stamp_point(npc_svo_grid_t *g, float x, float y, float z, uint16_t mat)
{
    phys_triangle_t tri;
    tri.v[0] = (phys_vec3_t){ x, y, z };
    tri.v[1] = (phys_vec3_t){ x + 0.01f, y, z };
    tri.v[2] = (phys_vec3_t){ x, y + 0.01f, z };
    lm_svo_stamp_triangle(g, &tri, mat);
}

/* A leaf samples its own material; a parent averages two differently-coloured
 * leaves. Table: id 1 -> red diffuse, id 2 -> blue diffuse. */
static int test_leaf_and_parent_average(void)
{
    npc_svo_grid_t g;
    phys_aabb_t b = { { 0, 0, 0 }, { 1, 1, 1 } };
    ASSERT_TRUE(npc_svo_grid_init(&g, b, 4));

    /* Two adjacent voxels that share a parent at the coarsest split. */
    stamp_point(&g, 0.10f, 0.10f, 0.10f, 1); /* red */
    stamp_point(&g, 0.90f, 0.90f, 0.90f, 2); /* blue */

    lm_material_t entries[3] = {
        { { 0, 0, 0 }, { 0, 0, 0 } },
        { { 1, 0, 0 }, { 0, 0, 0 } }, /* id 1 red */
        { { 0, 0, 1 }, { 0, 0, 0 } }, /* id 2 blue */
    };
    lm_material_table_t table = { entries, 3, { { 0, 0, 0 }, { 0, 0, 0 } } };

    lm_svo_mip_build(&g, &table);

    /* The root (node 0) averages the whole tree: two solid leaves, one red one
     * blue, equal weight -> (0.5, 0, 0.5). */
    lm_svo_shade_t root = lm_svo_mip_sample(&g, 0u, 0u);
    ASSERT_TRUE(NEAR(root.diffuse.x, 0.5f) && NEAR(root.diffuse.y, 0.0f) &&
                NEAR(root.diffuse.z, 0.5f));

    npc_svo_grid_destroy(&g);
    return 0;
}

/* Sampling a leaf directly returns its pure material colour; climbing to the
 * root blends toward the average. */
static int test_sample_climb(void)
{
    npc_svo_grid_t g;
    phys_aabb_t b = { { 0, 0, 0 }, { 1, 1, 1 } };
    ASSERT_TRUE(npc_svo_grid_init(&g, b, 4));
    stamp_point(&g, 0.10f, 0.10f, 0.10f, 1); /* red */
    stamp_point(&g, 0.90f, 0.90f, 0.90f, 2); /* blue */
    lm_material_t entries[3] = {
        { { 0, 0, 0 }, { 0, 0, 0 } }, { { 1, 0, 0 }, { 0, 0, 0 } },
        { { 0, 0, 1 }, { 0, 0, 0 } },
    };
    lm_material_table_t table = { entries, 3, { { 0, 0, 0 }, { 0, 0, 0 } } };
    lm_svo_mip_build(&g, &table);

    /* Find the red leaf by tracing a point query. */
    uint32_t leaf = NPC_SVO_INVALID_NODE;
    npc_svo_query_point(&g, (phys_vec3_t){ 0.10f, 0.10f, 0.10f }, &leaf);
    ASSERT_TRUE(leaf != NPC_SVO_INVALID_NODE);
    lm_svo_shade_t at_leaf = lm_svo_mip_sample(&g, leaf, 0u);
    ASSERT_TRUE(NEAR(at_leaf.diffuse.x, 1.0f) && NEAR(at_leaf.diffuse.z, 0.0f));
    /* Climbing all the way up (large levels_up clamps at root) blends. */
    lm_svo_shade_t high = lm_svo_mip_sample(&g, leaf, 64u);
    ASSERT_TRUE(high.diffuse.x < 0.99f && high.diffuse.z > 0.01f);
    npc_svo_grid_destroy(&g);
    return 0;
}

/* NULL / empty inputs are safe. */
static int test_safety(void)
{
    lm_svo_mip_build(NULL, NULL);
    lm_svo_shade_t s = lm_svo_mip_sample(NULL, 0u, 0u);
    ASSERT_TRUE(NEAR(s.diffuse.x, 0.0f) && NEAR(s.emissive.z, 0.0f));
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "leaf_and_parent_average", test_leaf_and_parent_average },
    { "sample_climb", test_sample_climb },
    { "safety", test_safety },
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
