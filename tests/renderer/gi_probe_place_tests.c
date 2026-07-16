/**
 * @file gi_probe_place_tests.c
 * @brief Unit tests for probe placement seeding (rpg-qthg).
 */
#include <math.h>
#include <stdio.h>

#include "ferrum/renderer/gi/gi_probe_place.h"

#define ASSERT_TRUE(cond)                                                    \
    do { if (!(cond)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                               #cond); return 1; } } while (0)

static int test_seed_box(void)
{
    float pos[64 * 3], sh[64 * 27];
    gi_probe_set_t set;
    gi_probe_set_init(&set, pos, sh, 64u);

    /* [0,4]^3 at spacing 2 -> 2 cells/axis, centres at 1 and 3 -> 8 probes. */
    float mn[3] = { 0, 0, 0 }, mx[3] = { 4, 4, 4 };
    uint32_t n = gi_probe_seed_box(&set, mn, mx, 2.0f);
    ASSERT_TRUE(n == 8u);
    ASSERT_TRUE(set.count == 8u);

    /* First probe sits at the first cell centre (1,1,1). */
    ASSERT_TRUE(fabsf(set.pos[0] - 1.0f) < 1e-5f);
    ASSERT_TRUE(fabsf(set.pos[1] - 1.0f) < 1e-5f);
    ASSERT_TRUE(fabsf(set.pos[2] - 1.0f) < 1e-5f);

    /* Every probe lies inside the box. */
    for (uint32_t i = 0; i < set.count; ++i)
        for (int a = 0; a < 3; ++a)
            ASSERT_TRUE(set.pos[i*3+a] >= mn[a] && set.pos[i*3+a] <= mx[a]);
    return 0;
}

static int test_seed_capacity(void)
{
    float pos[4 * 3], sh[4 * 27];
    gi_probe_set_t set;
    gi_probe_set_init(&set, pos, sh, 4u);
    /* Would be 27 probes, but capacity is 4 -> stops at 4, no overflow. */
    float mn[3] = { 0, 0, 0 }, mx[3] = { 6, 6, 6 };
    uint32_t n = gi_probe_seed_box(&set, mn, mx, 2.0f);
    ASSERT_TRUE(n == 4u);
    ASSERT_TRUE(set.count == 4u);
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_seed_box();
    rc |= test_seed_capacity();
    if (rc == 0)
        printf("  OK: all gi_probe_place tests passed\n");
    return rc;
}
