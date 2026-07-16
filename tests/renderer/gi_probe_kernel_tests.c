/**
 * @file gi_probe_kernel_tests.c
 * @brief Unit tests for the probe update kernel: cone-trace the combined SDF to
 *        dynamic lights and accumulate SH9 per probe (rpg-p3w3).
 */
#include <math.h>
#include <stdio.h>

#include "ferrum/renderer/gi/gi_probe_kernel.h"
#include "ferrum/lightmap/lm_sh.h"

#define ASSERT_TRUE(cond)                                                    \
    do { if (!(cond)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                               #cond); return 1; } } while (0)

/* Reconstruct one channel's irradiance for probe @p i toward @p normal. */
static float irr(const gi_probe_set_t *set, uint32_t i, int ch, vec3_t normal)
{
    lm_sh9_t sh;
    for (int b = 0; b < 9; ++b) sh.c[b] = set->sh[i * 27 + ch * 9 + b];
    return lm_sh9_irradiance(&sh, normal);
}

static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }

static int test_point_unoccluded(void)
{
    float pos[3], sh[27];
    gi_probe_set_t set;
    gi_probe_set_init(&set, pos, sh, 1u);
    gi_probe_add(&set, 0, 0, 0);                        /* probe at origin. */

    gi_light_t light = { GI_LIGHT_POINT, { 0, 5, 0 }, { 0, 0, 0 },
                         { 1, 1, 1 }, 20.0f, 0, 0 };   /* white, above, in range. */
    gi_probe_kernel_update(&set, 0u, 1u, NULL, NULL, NULL, 0.0f,
                           NULL, 0u, &light, 1u, 48u, 8.0f);

    /* Lit from above: irradiance toward +Y > toward -Y, and positive. */
    float up = irr(&set, 0, 0, v3(0, 1, 0));
    float down = irr(&set, 0, 0, v3(0, -1, 0));
    ASSERT_TRUE(up > 0.01f);
    ASSERT_TRUE(up > down);
    return 0;
}

static int test_point_occluded(void)
{
    float pos[3], sh[27];
    gi_probe_set_t set;
    gi_probe_set_init(&set, pos, sh, 1u);
    gi_probe_add(&set, 0, 0, 0);

    gi_light_t light = { GI_LIGHT_POINT, { 0, 5, 0 }, { 0, 0, 0 },
                         { 1, 1, 1 }, 20.0f, 0, 0 };
    /* A box collider between the probe and the light blocks it. */
    gi_collider_t box = { GI_COLLIDER_BOX, { 0, 2.5f, 0 }, { 0, 0, 0 },
                          { 1, 0.3f, 1 } };

    gi_probe_kernel_update(&set, 0u, 1u, NULL, NULL, NULL, 0.0f,
                           &box, 1u, &light, 1u, 64u, 8.0f);
    float up_occ = irr(&set, 0, 0, v3(0, 1, 0));

    /* Same setup with no occluder should be much brighter. */
    gi_probe_set_reset(&set); gi_probe_add(&set, 0, 0, 0);
    gi_probe_kernel_update(&set, 0u, 1u, NULL, NULL, NULL, 0.0f,
                           NULL, 0u, &light, 1u, 64u, 8.0f);
    float up_free = irr(&set, 0, 0, v3(0, 1, 0));

    ASSERT_TRUE(up_occ < up_free * 0.5f); /* occluder darkens it substantially. */
    return 0;
}

static int test_directional(void)
{
    float pos[3], sh[27];
    gi_probe_set_t set;
    gi_probe_set_init(&set, pos, sh, 1u);
    gi_probe_add(&set, 0, 0, 0);

    /* Sun travelling straight down -> lights the up-facing hemisphere. */
    gi_light_t sun = { GI_LIGHT_DIRECTIONAL, { 0, 0, 0 }, { 0, -1, 0 },
                       { 1, 1, 1 }, 0, 0, 0 };
    gi_probe_kernel_update(&set, 0u, 1u, NULL, NULL, NULL, 0.0f,
                           NULL, 0u, &sun, 1u, 48u, 8.0f);
    ASSERT_TRUE(irr(&set, 0, 0, v3(0, 1, 0)) > 0.01f);
    return 0;
}

static int test_out_of_range(void)
{
    float pos[3], sh[27];
    gi_probe_set_t set;
    gi_probe_set_init(&set, pos, sh, 1u);
    gi_probe_add(&set, 0, 0, 0);
    gi_light_t light = { GI_LIGHT_POINT, { 0, 100, 0 }, { 0, 0, 0 },
                         { 1, 1, 1 }, 5.0f, 0, 0 };   /* 100m away, range 5. */
    gi_probe_kernel_update(&set, 0u, 1u, NULL, NULL, NULL, 0.0f,
                           NULL, 0u, &light, 1u, 48u, 8.0f);
    ASSERT_TRUE(irr(&set, 0, 0, v3(0, 1, 0)) < 0.001f);   /* out of range -> dark. */
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_point_unoccluded();
    rc |= test_point_occluded();
    rc |= test_directional();
    rc |= test_out_of_range();
    if (rc == 0)
        printf("  OK: all gi_probe_kernel tests passed\n");
    return rc;
}
