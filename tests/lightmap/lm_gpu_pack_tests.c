/**
 * @file lm_gpu_pack_tests.c
 * @brief Unit tests for packing the offline lightmap scene into GPU (SSBO)
 *        buffers for the compute-shader gather (no GL). Builds raw structs by
 *        hand so the packing is exercised without the npc/lightmap lifecycles.
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/lightmap/gpu/lm_gpu_pack.h"

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "ASSERT failed %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                   \
            return 1;                                                         \
        }                                                                     \
    } while (0)
#define EQF(a, b) ((a) == (b))

/* Build a 3-node SVO by hand: root with two leaf children. */
static void make_svo(npc_svo_grid_t *g, npc_svo_node_t *nodes) {
    memset(nodes, 0, sizeof(npc_svo_node_t) * 3u);
    for (int n = 0; n < 3; ++n)
        for (int c = 0; c < 8; ++c) nodes[n].children[c] = NPC_SVO_INVALID_NODE;
    nodes[0].children[0] = 1u; nodes[0].children[7] = 2u; /* root -> leaves. */
    nodes[1].diffuse[0] = 0.5f; nodes[1].diffuse[1] = 0.25f; nodes[1].diffuse[2] = 0.1f;
    nodes[2].emissive[0] = 3.0f; nodes[2].emissive[1] = 2.0f; nodes[2].emissive[2] = 1.0f;
    memset(g, 0, sizeof(*g));
    g->nodes = nodes; g->node_count = 3u; g->voxel_size = 0.06f; g->max_depth = 8u;
    g->world_bounds.min = (phys_vec3_t){ -1, 0, -7 };
    g->world_bounds.max = (phys_vec3_t){ 10, 4.9f, 1 };
}

/* Node packing copies children (incl. INVALID sentinels) + diffuse/emissive. */
static int test_pack_nodes(void) {
    npc_svo_node_t nodes[3]; npc_svo_grid_t g; make_svo(&g, nodes);
    lm_gpu_node_t out[3];
    ASSERT_TRUE(lm_gpu_pack_nodes(&g, out, 3u) == 3u);
    ASSERT_TRUE(out[0].children[0] == 1u && out[0].children[7] == 2u);
    ASSERT_TRUE(out[0].children[1] == NPC_SVO_INVALID_NODE);
    ASSERT_TRUE(EQF(out[1].diffuse[0], 0.5f) && EQF(out[1].diffuse[1], 0.25f));
    ASSERT_TRUE(EQF(out[2].emissive[0], 3.0f) && EQF(out[2].emissive[2], 1.0f));
    ASSERT_TRUE(sizeof(lm_gpu_node_t) == 64u); /* std430 stride. */
    return 0;
}

/* Insufficient capacity reports the required count and writes nothing. */
static int test_pack_nodes_capacity(void) {
    npc_svo_node_t nodes[3]; npc_svo_grid_t g; make_svo(&g, nodes);
    lm_gpu_node_t out[3]; memset(out, 0xAB, sizeof out);
    ASSERT_TRUE(lm_gpu_pack_nodes(&g, out, 2u) == 3u);         /* query count. */
    ASSERT_TRUE(out[0].children[0] == 0xABABABABu);            /* untouched. */
    return 0;
}

/* Luxel packing pulls world pos + normal from each luxel. */
static int test_pack_luxels(void) {
    lm_luxel_t lx[2]; memset(lx, 0, sizeof lx);
    lx[0].pos = (vec3_t){ 1, 2, 3 }; lx[0].normal = (vec3_t){ 0, 1, 0 };
    lx[1].pos = (vec3_t){ 4, 5, 6 }; lx[1].normal = (vec3_t){ 1, 0, 0 };
    lm_lightmap_t lm; memset(&lm, 0, sizeof lm);
    lm.luxels = lx; lm.res_u = 2u; lm.res_v = 1u;
    lm_gpu_luxel_t out[2];
    ASSERT_TRUE(lm_gpu_pack_luxels(&lm, out, 2u) == 2u);
    ASSERT_TRUE(EQF(out[0].pos[0], 1.0f) && EQF(out[0].pos[2], 3.0f));
    ASSERT_TRUE(EQF(out[1].normal[0], 1.0f) && EQF(out[1].normal[1], 0.0f));
    ASSERT_TRUE(sizeof(lm_gpu_luxel_t) == 32u);
    return 0;
}

/* Light packing encodes kind + range + cone cosines into the vec4 padding. */
static int test_pack_lights(void) {
    lm_light_t lts[1]; memset(lts, 0, sizeof lts);
    lts[0].kind = LM_LIGHT_DIRECTIONAL;
    lts[0].direction = (vec3_t){ 0.15f, -0.42f, 0.90f };
    lts[0].color = (vec3_t){ 3.6f, 3.4f, 3.0f };
    lm_gpu_light_t out[1];
    ASSERT_TRUE(lm_gpu_pack_lights(lts, 1u, out, 1u) == 1u);
    ASSERT_TRUE((uint32_t)out[0].position[3] == (uint32_t)LM_LIGHT_DIRECTIONAL);
    ASSERT_TRUE(EQF(out[0].direction[0], 0.15f) && EQF(out[0].color[2], 3.0f));
    ASSERT_TRUE(sizeof(lm_gpu_light_t) == 64u);
    return 0;
}

/* Params capture the grid + gather config. */
static int test_pack_params(void) {
    npc_svo_node_t nodes[3]; npc_svo_grid_t g; make_svo(&g, nodes);
    lm_gpu_params_t p;
    lm_gpu_pack_params(&g, 42u /*luxels*/, 1u /*lights*/, 1.5f /*transition*/,
                       80.0f /*maxdist*/, 3u /*bounces*/, &p);
    ASSERT_TRUE(EQF(p.bounds_min[3], 0.06f));   /* voxel_size in w. */
    ASSERT_TRUE(EQF(p.bounds_max[3], 1.5f));    /* transition in w. */
    ASSERT_TRUE(EQF(p.misc[0], 80.0f));         /* maxdist. */
    ASSERT_TRUE(p.counts[0] == 3u && p.counts[1] == 42u && p.counts[2] == 1u && p.counts[3] == 3u);
    return 0;
}

/* NULL args are safe. */
static int test_null_safe(void) {
    lm_gpu_node_t nout[1]; lm_gpu_luxel_t lxout[1]; lm_gpu_light_t ltout[1];
    ASSERT_TRUE(lm_gpu_pack_nodes(NULL, nout, 1u) == 0u);
    ASSERT_TRUE(lm_gpu_pack_luxels(NULL, lxout, 1u) == 0u);
    ASSERT_TRUE(lm_gpu_pack_lights(NULL, 1u, ltout, 1u) == 0u);
    lm_gpu_pack_params(NULL, 0u, 0u, 0.0f, 0.0f, 0u, NULL); /* no crash. */
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "pack_nodes", test_pack_nodes },
    { "pack_nodes_capacity", test_pack_nodes_capacity },
    { "pack_luxels", test_pack_luxels },
    { "pack_lights", test_pack_lights },
    { "pack_params", test_pack_params },
    { "null_safe", test_null_safe },
};

int main(void) {
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
