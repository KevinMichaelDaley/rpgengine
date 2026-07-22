/*
 * @file shadow_csm_grid_tests.c
 * @brief Unit tests for the light-frustum cascade tiling math (rpg-7s4y).
 *
 * The cached directional CSM partitions the LIGHT frustum into a grid of tiles
 * (one per cascade) rather than binning casters by size. These tests cover the
 * pure grid-factor helper -- given a cascade count and the light-space aspect
 * ratio, it must return a cols x rows factor pair that (1) multiplies to exactly
 * the cascade count when a factorisation exists, and (2) is the pair closest to
 * the target aspect (so tiles stay square-ish and texel density is even).
 */
#include "ferrum/renderer/shadow_csm.h"
#include <stdio.h>
#include <math.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++g_fail; } \
} while (0)

/* Happy path: a count with a near-aspect factorisation returns exactly that. */
static void test_exact_factor_landscape(void)
{
    uint32_t cols = 0, rows = 0;
    /* 8 cascades over a 1.31:1 (landscape) light frustum -> 4x2 (aspect 2.0 is
     * the closest factor pair of 8 to 1.31; 2x4 would be 0.5, further off). */
    shadow_csm_grid_dims(8u, 1.31f, &cols, &rows);
    CHECK(cols * rows == 8u, "8 cascades must tile exactly (cols*rows==8)");
    CHECK(cols == 4u && rows == 2u, "8 @ aspect 1.31 -> 4x2");
}

/* A portrait aspect flips the orientation. */
static void test_exact_factor_portrait(void)
{
    uint32_t cols = 0, rows = 0;
    shadow_csm_grid_dims(8u, 0.5f, &cols, &rows);   /* tall frustum. */
    CHECK(cols * rows == 8u, "portrait 8 tiles exactly");
    CHECK(cols == 2u && rows == 4u, "8 @ aspect 0.5 -> 2x4");
}

/* Square aspect prefers the most balanced pair. */
static void test_square(void)
{
    uint32_t cols = 0, rows = 0;
    shadow_csm_grid_dims(9u, 1.0f, &cols, &rows);
    CHECK(cols == 3u && rows == 3u, "9 @ aspect 1.0 -> 3x3");
    shadow_csm_grid_dims(4u, 1.0f, &cols, &rows);
    CHECK(cols == 2u && rows == 2u, "4 @ aspect 1.0 -> 2x2");
}

/* Edge: a single cascade is a 1x1 tile (the whole frustum). */
static void test_single(void)
{
    uint32_t cols = 0, rows = 0;
    shadow_csm_grid_dims(1u, 1.31f, &cols, &rows);
    CHECK(cols == 1u && rows == 1u, "1 cascade -> 1x1");
}

/* Edge: a prime count has no balanced factorisation -> falls back to a single
 * strip along the longer axis (Nx1 for landscape). The product must still cover
 * every cascade (cols*rows >= n) so no tile index is left unassigned. */
static void test_prime(void)
{
    uint32_t cols = 0, rows = 0;
    shadow_csm_grid_dims(7u, 1.31f, &cols, &rows);
    CHECK(cols * rows >= 7u, "7 cascades: grid must cover all (cols*rows>=7)");
    CHECK(cols >= rows, "landscape prime -> wider than tall");
    shadow_csm_grid_dims(5u, 0.5f, &cols, &rows);
    CHECK(cols * rows >= 5u, "5 cascades covered");
    CHECK(rows >= cols, "portrait prime -> taller than wide");
}

/* Edge: zero cascades is a no-op guarded to 1x1 (never divide by zero). */
static void test_zero(void)
{
    uint32_t cols = 9, rows = 9;
    shadow_csm_grid_dims(0u, 1.0f, &cols, &rows);
    CHECK(cols >= 1u && rows >= 1u, "0 cascades clamps to a valid 1x1+");
}

int main(void)
{
    test_exact_factor_landscape();
    test_exact_factor_portrait();
    test_square();
    test_single();
    test_prime();
    test_zero();
    if (g_fail == 0) printf("shadow_csm_grid_tests: all passed\n");
    return g_fail ? 1 : 0;
}
