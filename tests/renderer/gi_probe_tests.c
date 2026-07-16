/**
 * @file gi_probe_tests.c
 * @brief Unit tests for the adaptive irradiance probe set (gi_probe_set) and its
 *        spatial lookup accel grid (gi_probe_grid) -- rpg-qthg.
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gi/gi_probe_set.h"
#include "ferrum/renderer/gi/gi_probe_grid.h"

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define CAP 64u

static int test_probe_set(void)
{
    float pos[CAP * 3], sh[CAP * 27];
    gi_probe_set_t set;
    gi_probe_set_init(&set, pos, sh, CAP);
    ASSERT_TRUE(set.count == 0 && set.capacity == CAP);

    ASSERT_TRUE(gi_probe_add(&set, 1.0f, 2.0f, 3.0f) == 0);
    ASSERT_TRUE(gi_probe_add(&set, -4.0f, 5.0f, 6.0f) == 1);
    ASSERT_TRUE(set.count == 2);
    ASSERT_TRUE(set.pos[0] == 1.0f && set.pos[1] == 2.0f && set.pos[2] == 3.0f);
    ASSERT_TRUE(set.pos[3] == -4.0f && set.pos[4] == 5.0f && set.pos[5] == 6.0f);
    /* SH cleared on add so an un-updated probe contributes nothing. */
    for (int c = 0; c < 27; ++c) ASSERT_TRUE(set.sh[c] == 0.0f);

    gi_probe_set_reset(&set);
    ASSERT_TRUE(set.count == 0);
    return 0;
}

static int test_probe_set_full(void)
{
    float pos[3 * 3], sh[3 * 27];
    gi_probe_set_t set;
    gi_probe_set_init(&set, pos, sh, 3u);
    ASSERT_TRUE(gi_probe_add(&set, 0, 0, 0) == 0);
    ASSERT_TRUE(gi_probe_add(&set, 0, 0, 0) == 1);
    ASSERT_TRUE(gi_probe_add(&set, 0, 0, 0) == 2);
    ASSERT_TRUE(gi_probe_add(&set, 0, 0, 0) == -1); /* full. */
    ASSERT_TRUE(set.count == 3);
    return 0;
}

static int test_probe_grid(void)
{
    /* [0,8]^3 box, cell size 2 -> 4x4x4. Probes 1,2 are one cell from the origin
     * cell (0,0,0); probe 3 is at the opposite corner cell (3,3,3), far away. */
    float pos[CAP * 3], sh[CAP * 27];
    gi_probe_set_t set;
    gi_probe_set_init(&set, pos, sh, CAP);
    gi_probe_add(&set, 0.5f, 0.5f, 0.5f);   /* cell (0,0,0) */
    gi_probe_add(&set, 3.5f, 0.5f, 0.5f);   /* cell (1,0,0) -- neighbour */
    gi_probe_add(&set, 0.5f, 3.5f, 0.5f);   /* cell (0,1,0) -- neighbour */
    gi_probe_add(&set, 7.5f, 7.5f, 7.5f);   /* cell (3,3,3) -- far */

    float amin[3] = { 0, 0, 0 }, amax[3] = { 8, 8, 8 };
    uint32_t cell_start[128], probe_idx[CAP];
    gi_probe_grid_t grid;
    ASSERT_TRUE(gi_probe_grid_build(&grid, &set, amin, amax, 2.0f,
                                    cell_start, 128u, probe_idx, CAP));
    ASSERT_TRUE(grid.dims[0] == 4 && grid.dims[1] == 4 && grid.dims[2] == 4);
    ASSERT_TRUE(grid.ncells == 64u);

    /* cell() maps a point to the right linear cell. */
    ASSERT_TRUE(gi_probe_grid_cell(&grid, 0.5f, 0.5f, 0.5f) ==
                gi_probe_grid_cell(&grid, 1.9f, 1.9f, 1.9f));   /* both in (0,0,0) */
    ASSERT_TRUE(gi_probe_grid_cell(&grid, 3.5f, 0.5f, 0.5f) !=
                gi_probe_grid_cell(&grid, 0.5f, 0.5f, 0.5f));

    /* gather() near the origin returns probe 0, and also its neighbours (probes
     * 1 and 2 are one cell over) -- i.e. more than just the exact cell. */
    uint32_t out[CAP];
    uint32_t n = gi_probe_grid_gather(&grid, 0.6f, 0.6f, 0.6f, out, CAP);
    ASSERT_TRUE(n >= 3u); /* corner (0,0,0) + neighbours (1,0,0),(0,1,0). */
    int saw0 = 0, saw1 = 0, saw2 = 0, saw3 = 0;
    for (uint32_t i = 0; i < n; ++i) {
        saw0 |= (out[i] == 0); saw1 |= (out[i] == 1);
        saw2 |= (out[i] == 2); saw3 |= (out[i] == 3);
    }
    ASSERT_TRUE(saw0 && saw1 && saw2);       /* neighbours gathered. */
    ASSERT_TRUE(!saw3);                        /* far corner NOT gathered. */

    /* Output cap is respected (never overflows). */
    uint32_t small[2];
    uint32_t m = gi_probe_grid_gather(&grid, 0.6f, 0.6f, 0.6f, small, 2u);
    ASSERT_TRUE(m <= 2u);
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_probe_set();
    rc |= test_probe_set_full();
    rc |= test_probe_grid();
    if (rc == 0)
        printf("  OK: all gi_probe tests passed\n");
    return rc;
}
