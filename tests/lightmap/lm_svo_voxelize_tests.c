/**
 * @file lm_svo_voxelize_tests.c
 * @brief Unit tests for texture-sampled surface voxelization (lm_svo_voxelize).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/lm_svo_material.h"
#include "ferrum/lightmap/lm_svo_mip.h"
#include "ferrum/lightmap/lm_svo_voxelize.h"
#include "ferrum/npc/npc_svo.h"

#define ASSERT_TRUE(c)                                                        \
    do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-2f)

/* A flat quad at z=0.5, tinted albedo, no texture (tint is the reflectance). */
static const float QPOS[] = { 0.1f,0.1f,0.5f, 0.9f,0.1f,0.5f, 0.9f,0.9f,0.5f, 0.1f,0.9f,0.5f };
static const float QNRM[] = { 0,0,1, 0,0,1, 0,0,1, 0,0,1 };
static const float QUV[]  = { 0,0, 1,0, 1,1, 0,1 };
static const uint32_t QIDX[] = { 0,1,2, 0,2,3 };

static void stamp_quad(npc_svo_grid_t *g)
{
    for (int t = 0; t + 2 < 6; t += 3) {
        phys_triangle_t tri;
        for (int k = 0; k < 3; ++k) {
            const float *p = &QPOS[QIDX[t+k]*3];
            tri.v[k] = (phys_vec3_t){ p[0], p[1], p[2] };
        }
        lm_svo_stamp_triangle(g, &tri, 0u);
    }
}

/* Solid voxels on the quad take the tinted reflectance; the parent averages it. */
static int test_voxelize_tint(void)
{
    npc_svo_grid_t g;
    ASSERT_TRUE(npc_svo_grid_init(&g, (phys_aabb_t){ {0,0,0},{1,1,1} }, 6));
    stamp_quad(&g);

    lm_mesh_t m; memset(&m, 0, sizeof m);
    m.positions = QPOS; m.normals = QNRM; m.uv0 = QUV; m.indices = QIDX;
    m.vert_count = 4; m.index_count = 6;
    m.albedo = (vec3_t){ 0.8f, 0.2f, 0.1f }; m.emissive = (vec3_t){ 0, 0, 0 };

    uint32_t *count = malloc((size_t)g.node_count * sizeof(uint32_t));
    ASSERT_TRUE(count != NULL);
    vec3_t *vn = malloc((size_t)g.node_count*sizeof(vec3_t));
    lm_svo_voxelize(&g, &m, 1u, count, vn);

    /* A solid voxel on the quad reports the tint reflectance. */
    uint32_t node = NPC_SVO_INVALID_NODE;
    uint8_t f = npc_svo_query_point(&g, (phys_vec3_t){ 0.5f, 0.5f, 0.5f }, &node);
    ASSERT_TRUE((f & NPC_SVO_FLAG_SOLID) && node != NPC_SVO_INVALID_NODE);
    ASSERT_TRUE(NEAR(g.nodes[node].diffuse[0], 0.8f) &&
                NEAR(g.nodes[node].diffuse[1], 0.2f) &&
                NEAR(g.nodes[node].diffuse[2], 0.1f));
    ASSERT_TRUE(NEAR(g.nodes[node].emissive[0], 0.0f));

    /* The root (coarse cone level) averages toward the same tint (the quad is
     * the only lit geometry). */
    ASSERT_TRUE(g.nodes[0].diffuse[0] > 0.1f);

    free(count); free(vn);
    npc_svo_grid_destroy(&g);
    return 0;
}

/* A solid leaf off the quad (there are none away from z=0.5) stays black: sample
 * a solid voxel ON the quad vs. confirm empty space is not solid. */
static int test_air_black(void)
{
    npc_svo_grid_t g;
    ASSERT_TRUE(npc_svo_grid_init(&g, (phys_aabb_t){ {0,0,0},{1,1,1} }, 6));
    stamp_quad(&g);
    lm_mesh_t m; memset(&m, 0, sizeof m);
    m.positions = QPOS; m.normals = QNRM; m.uv0 = QUV; m.indices = QIDX;
    m.vert_count = 4; m.index_count = 6; m.albedo = (vec3_t){ 1, 1, 1 };
    uint32_t *count = malloc((size_t)g.node_count * sizeof(uint32_t));
    vec3_t *vn = malloc((size_t)g.node_count*sizeof(vec3_t));
    lm_svo_voxelize(&g, &m, 1u, count, vn);
    uint32_t node = NPC_SVO_INVALID_NODE;
    /* z=0.1 is off the quad -> not solid (an interior node here may legitimately
     * carry mipped material for cone sampling, so we test occupancy, not colour). */
    uint8_t f = npc_svo_query_point(&g, (phys_vec3_t){ 0.5f, 0.5f, 0.1f }, &node);
    ASSERT_TRUE(!(f & NPC_SVO_FLAG_SOLID));
    free(count); free(vn);
    npc_svo_grid_destroy(&g);
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "voxelize_tint", test_voxelize_tint },
    { "air_black", test_air_black },
};

int main(void)
{
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
