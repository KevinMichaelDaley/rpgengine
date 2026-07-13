/**
 * @file lm_svo_material_tests.c
 * @brief Unit tests for lm_svo_material (material-stamping rasterization).
 */
#include <stdio.h>

#include "ferrum/lightmap/lm_svo_material.h"
#include "ferrum/npc/npc_svo.h"

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static phys_vec3_t v3(float x, float y, float z) { return (phys_vec3_t){ x, y, z }; }

/* Stamped voxels carry the surface's real material id. */
static int test_stamp_triangle(void)
{
    npc_svo_grid_t svo;
    phys_aabb_t bounds = { { -8, -8, -8 }, { 8, 8, 8 } };
    ASSERT_TRUE(npc_svo_grid_init(&svo, bounds, 5));

    phys_triangle_t t0 = { { v3(-4, -4, 2), v3(4, -4, 2), v3(4, 4, 2) } };
    phys_triangle_t t1 = { { v3(-4, -4, 2), v3(4, 4, 2), v3(-4, 4, 2) } };
    lm_svo_stamp_triangle(&svo, &t0, 7);
    lm_svo_stamp_triangle(&svo, &t1, 7);

    uint32_t node = NPC_SVO_INVALID_NODE;
    uint8_t flags = npc_svo_query_point(&svo, v3(0.13f, 0.07f, 2.0f), &node);
    ASSERT_TRUE(flags & NPC_SVO_FLAG_SOLID);
    ASSERT_TRUE(node != NPC_SVO_INVALID_NODE);
    ASSERT_TRUE(svo.nodes[node].material == 7);

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* A later surface of a different material overwrites a shared voxel. */
static int test_last_writer_wins(void)
{
    npc_svo_grid_t svo;
    phys_aabb_t bounds = { { -8, -8, -8 }, { 8, 8, 8 } };
    ASSERT_TRUE(npc_svo_grid_init(&svo, bounds, 5));

    phys_triangle_t t = { { v3(-2, -2, 1), v3(2, -2, 1), v3(2, 2, 1) } };
    lm_svo_stamp_triangle(&svo, &t, 3);
    lm_svo_stamp_triangle(&svo, &t, 9); /* same voxels, new material */

    uint32_t node = NPC_SVO_INVALID_NODE;
    npc_svo_query_point(&svo, v3(0.13f, -0.4f, 1.0f), &node);
    ASSERT_TRUE(node != NPC_SVO_INVALID_NODE);
    ASSERT_TRUE(svo.nodes[node].material == 9);

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* Mesh stamping tags every triangle's voxels. */
static int test_stamp_mesh(void)
{
    npc_svo_grid_t svo;
    phys_aabb_t bounds = { { -8, -8, -8 }, { 8, 8, 8 } };
    ASSERT_TRUE(npc_svo_grid_init(&svo, bounds, 5));

    phys_triangle_t tris[2] = {
        { { v3(-4, -4, 3), v3(4, -4, 3), v3(4, 4, 3) } },
        { { v3(-4, -4, 3), v3(4, 4, 3), v3(-4, 4, 3) } },
    };
    lm_svo_stamp_mesh(&svo, tris, 2, 5);

    uint32_t node = NPC_SVO_INVALID_NODE;
    uint8_t flags = npc_svo_query_point(&svo, v3(-1.2f, 1.3f, 3.0f), &node);
    ASSERT_TRUE(flags & NPC_SVO_FLAG_SOLID);
    ASSERT_TRUE(svo.nodes[node].material == 5);

    npc_svo_grid_destroy(&svo);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "stamp_triangle", test_stamp_triangle },
    { "last_writer_wins", test_last_writer_wins },
    { "stamp_mesh", test_stamp_mesh },
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
