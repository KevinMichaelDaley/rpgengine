/**
 * @file lm_farfield_tests.c
 * @brief Unit tests for lm_farfield (SVO distant-reflector gather).
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/lightmap/lm_farfield.h"
#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_svo_material.h"
#include "ferrum/lightmap/lm_svo_mip.h"
#include "ferrum/math/vec3.h"
#include "ferrum/npc/npc_svo.h"

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

enum { LM_EMIT_MAT = 2 }; /* material id of the emissive ceiling */

static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }
static phys_vec3_t p3(float x, float y, float z) { return (phys_vec3_t){ x, y, z }; }

/* One luxel at the origin with a chosen normal. */
static void build_single(lm_luxel_t *luxel, lm_lightmap_t *lm, vec3_t normal)
{
    memset(luxel, 0, sizeof(*luxel));
    luxel->pos = v3(0, 0, 0);
    luxel->normal = normal;
    luxel->albedo = v3(0.5f, 0.5f, 0.5f);
    for (int c = 0; c < 3; ++c)
        lm_sh9_zero(&luxel->sh[c]);
    lm->luxels = luxel;
    lm->res_u = 1;
    lm->res_v = 1;
}

/* An emissive ceiling plane at z=5, stamped into the SVO with a REAL material
 * id so the far-field pass reads its true emissive from the table. */
static bool build_ceiling(npc_svo_grid_t *svo)
{
    phys_aabb_t bounds = { { -8, -8, -2 }, { 8, 8, 8 } };
    if (!npc_svo_grid_init(svo, bounds, 5))
        return false;
    phys_triangle_t t0 = { { p3(-4, -4, 5), p3(4, -4, 5), p3(4, 4, 5) } };
    phys_triangle_t t1 = { { p3(-4, -4, 5), p3(4, 4, 5), p3(-4, 4, 5) } };
    lm_svo_stamp_triangle(svo, &t0, LM_EMIT_MAT);
    lm_svo_stamp_triangle(svo, &t1, LM_EMIT_MAT);
    return true;
}

/* Table where only LM_EMIT_MAT is emissive; every other id (and the fallback)
 * is dark, so a real material lookup -- not a catch-all -- drives the result. */
static lm_material_table_t emissive_table(const lm_material_t *entries)
{
    lm_material_table_t table;
    table.entries = entries;
    table.count = LM_EMIT_MAT + 1;
    table.fallback = (lm_material_t){ { 0, 0, 0 }, { 0, 0, 0 } };
    return table;
}

static float luxel_irradiance(lm_lightmap_t *lm)
{
    return lm_sh9_irradiance(&lm->luxels[0].sh[0], lm->luxels[0].normal);
}

/* Happy: a far emissive wall lights a luxel facing it. */
static int test_far_emissive(void)
{
    lm_luxel_t luxel;
    lm_lightmap_t lm;
    build_single(&luxel, &lm, v3(0, 0, 1));
    npc_svo_grid_t svo;
    ASSERT_TRUE(build_ceiling(&svo));
    lm_material_t entries[3] = { { { 0, 0, 0 }, { 0, 0, 0 } },
                                { { 0, 0, 0 }, { 0, 0, 0 } },
                                { { 0.5f, 0.5f, 0.5f }, { 5, 5, 5 } } };
    lm_material_table_t table = emissive_table(entries);
    lm_svo_mip_build(&svo, &table);

    lm_farfield_gather(&lm, &svo, &table, NULL, 256, 1.0f, 20.0f, 42u);
    ASSERT_TRUE(luxel_irradiance(&lm) > 0.0f);
    npc_svo_grid_destroy(&svo);
    return 0;
}

/* Near hits (t <= near_radius) are ignored to avoid double counting. */
static int test_near_ignored(void)
{
    lm_luxel_t luxel;
    lm_lightmap_t lm;
    build_single(&luxel, &lm, v3(0, 0, 1));
    npc_svo_grid_t svo;
    ASSERT_TRUE(build_ceiling(&svo));
    lm_material_t entries[3] = { { { 0, 0, 0 }, { 0, 0, 0 } },
                                { { 0, 0, 0 }, { 0, 0, 0 } },
                                { { 0.5f, 0.5f, 0.5f }, { 5, 5, 5 } } };
    lm_material_table_t table = emissive_table(entries);
    lm_svo_mip_build(&svo, &table);

    /* near_radius past the wall at z=5 -> every hit is "near" -> skipped. */
    lm_farfield_gather(&lm, &svo, &table, NULL, 256, 10.0f, 20.0f, 42u);
    ASSERT_TRUE(luxel_irradiance(&lm) <= 1e-6f);
    npc_svo_grid_destroy(&svo);
    return 0;
}

/* A luxel facing away from the wall gathers nothing. */
static int test_backface(void)
{
    lm_luxel_t luxel;
    lm_lightmap_t lm;
    build_single(&luxel, &lm, v3(0, 0, -1)); /* faces down, away from ceiling */
    npc_svo_grid_t svo;
    ASSERT_TRUE(build_ceiling(&svo));
    lm_material_t entries[3] = { { { 0, 0, 0 }, { 0, 0, 0 } },
                                { { 0, 0, 0 }, { 0, 0, 0 } },
                                { { 0.5f, 0.5f, 0.5f }, { 5, 5, 5 } } };
    lm_material_table_t table = emissive_table(entries);
    lm_svo_mip_build(&svo, &table);

    lm_farfield_gather(&lm, &svo, &table, NULL, 256, 1.0f, 20.0f, 42u);
    ASSERT_TRUE(luxel_irradiance(&lm) <= 1e-6f);
    npc_svo_grid_destroy(&svo);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    { "far_emissive", test_far_emissive },
    { "near_ignored", test_near_ignored },
    { "backface", test_backface },
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
