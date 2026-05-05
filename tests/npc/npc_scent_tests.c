/**
 * @file npc_scent_tests.c
 * @brief Wind-and-scent simulation tests: init/destroy, emit, advect,
 *        sample, multi-type independence.
 *
 * Covers:
 * - Init allocates, destroy frees
 * - Emit deposits intensity at the correct grid cell
 * - Advect shifts scent downwind with damping
 * - Sample returns the correct trilinear concentration
 * - Multiple scent types do not interfere
 */

#include "ferrum/npc/npc_sense_scent.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RUN(fn) \
    do { \
        printf("  %-48s ", #fn); \
        fn(); \
    } while (0)

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            printf("FAIL (%s:%d)\n", __FILE__, __LINE__); \
            g_fail++; \
            return; \
        } \
    } while (0)

#define ASSERT_INT_EQ(exp, act) \
    do { \
        if ((exp) != (act)) { \
            printf("FAIL (%s:%d) expected %d got %d\n", \
                   __FILE__, __LINE__, (int)(exp), (int)(act)); \
            g_fail++; \
            return; \
        } \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol) \
    do { \
        if (fabsf((exp) - (act)) > (tol)) { \
            printf("FAIL (%s:%d) expected %.6f got %.6f\n", \
                   __FILE__, __LINE__, (float)(exp), (float)(act)); \
            g_fail++; \
            return; \
        } \
    } while (0)

#define PASS() \
    do { \
        printf("PASS\n"); \
        g_pass++; \
    } while (0)

static int g_pass = 0;
static int g_fail = 0;

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

/* 1. Init / destroy */
static void test_init_destroy(void)
{
    npc_scent_field_t f;
    bool ok = npc_scent_field_init(&f, 8, 10.0f);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(8, f.res);
    ASSERT_TRUE(f.grid != NULL);
    ASSERT_TRUE(fabsf(f.cell_size - 10.0f) < 0.001f);

    /* Verify grid is zeroed. */
    size_t sz = (size_t)8 * 8 * 8 * NPC_SCENT_MAX_TYPES;
    size_t i;
    for (i = 0; i < sz; i++)
        ASSERT_TRUE(f.grid[i] == 0.0f);

    npc_scent_field_destroy(&f);
    ASSERT_TRUE(f.grid == NULL);
    ASSERT_INT_EQ(0, f.res);
    PASS();
}

/* 2. Destroy handles NULL gracefully. */
static void test_destroy_null(void)
{
    npc_scent_field_destroy(NULL);
    PASS();
}

/* 3. Emit deposits intensity at the correct cell. */
static void test_emit_scent_at_cell(void)
{
    npc_scent_field_t f;
    npc_scent_field_init(&f, 32, 1.0f);

    npc_scent_emitter_t em;
    em.pos[0]   = 5.0f;
    em.pos[1]   = 5.0f;
    em.pos[2]   = 5.0f;
    em.type     = NPC_SCENT_BLOOD;
    em.intensity = 3.5f;
    em.remaining_ticks = 100;

    npc_scent_emit(&f, &em);

    /* Cell (5,5,5) should have 3.5 for BLOOD. */
    int off = (5 * 32 * 32 + 5 * 32 + 5) * NPC_SCENT_MAX_TYPES + NPC_SCENT_BLOOD;
    ASSERT_FLOAT_NEAR(3.5f, f.grid[off], 0.001f);

    /* Other types at same cell should be 0. */
    ASSERT_FLOAT_NEAR(0.0f, f.grid[off + 1], 0.001f);

    npc_scent_field_destroy(&f);
    PASS();
}

/* 4. Emit clamps out-of-bounds position. */
static void test_emit_clamp_bounds(void)
{
    npc_scent_field_t f;
    npc_scent_field_init(&f, 32, 1.0f);

    npc_scent_emitter_t em;
    em.pos[0]   = -10.0f;  /* clamped to 0 */
    em.pos[1]   = 50.0f;   /* clamped to 31 */
    em.pos[2]   = 10.0f;
    em.type     = NPC_SCENT_FOOD;
    em.intensity = 2.0f;
    em.remaining_ticks = 0;

    npc_scent_emit(&f, &em);

    /* Should land in cell (0, 31, 10). */
    int off = (10 * 32 * 32 + 31 * 32 + 0) * NPC_SCENT_MAX_TYPES + NPC_SCENT_FOOD;
    ASSERT_FLOAT_NEAR(2.0f, f.grid[off], 0.001f);

    npc_scent_field_destroy(&f);
    PASS();
}

/* 5. Advect shifts scent downwind. */
static void test_advect_shifts_scent(void)
{
    npc_scent_field_t f;
    npc_scent_field_init(&f, 32, 1.0f);

    /* Wind blows east → each tick scent shifts +X by 1 cell. */
    f.wind[0] = 1.0f;
    f.wind[1] = 0.0f;
    f.wind[2] = 0.0f;

    /* Emit at (5, 10, 10). */
    npc_scent_emitter_t em;
    em.pos[0]   = 5.0f;
    em.pos[1]   = 10.0f;
    em.pos[2]   = 10.0f;
    em.type     = NPC_SCENT_SMOKE;
    em.intensity = 10.0f;
    em.remaining_ticks = 10;

    npc_scent_emit(&f, &em);

    /* After one advection step (dt = 1, wind = 1 → shift by 1 cell),
     * scent from cell (5,10,10) should arrive at (6,10,10), damped. */
    npc_scent_advect(&f, 1.0f);

    int off_old = (10 * 32 * 32 + 10 * 32 + 5) * NPC_SCENT_MAX_TYPES + NPC_SCENT_SMOKE;
    int off_new = (10 * 32 * 32 + 10 * 32 + 6) * NPC_SCENT_MAX_TYPES + NPC_SCENT_SMOKE;

    ASSERT_FLOAT_NEAR(0.0f, f.grid[off_old], 0.001f);   /* old cell empty */
    ASSERT_FLOAT_NEAR(10.0f * 0.95f, f.grid[off_new], 0.001f); /* damped */

    npc_scent_field_destroy(&f);
    PASS();
}

/* 6. Sample returns correct trilinear concentration. */
static void test_sample_correct_concentration(void)
{
    npc_scent_field_t f;
    npc_scent_field_init(&f, 32, 1.0f);

    /* Emit at (5, 10, 10) — exact cell corner. */
    npc_scent_emitter_t em;
    em.pos[0]   = 5.0f;
    em.pos[1]   = 10.0f;
    em.pos[2]   = 10.0f;
    em.type     = NPC_SCENT_BLOOD;
    em.intensity = 8.0f;
    em.remaining_ticks = 0;

    npc_scent_emit(&f, &em);

    /* Sample at world (5.0, 10.0, 10.0) → grid coords (5, 10, 10).
     * This is exactly the lower corner of cell (5,10,10),
     * so trilinear picks only that cell → full value. */
    npc_scent_sample_t out;
    bool hit = npc_scent_sample(&f, 5.0f, 10.0f, 10.0f, &out);
    ASSERT_TRUE(hit);
    ASSERT_INT_EQ(NPC_SCENT_BLOOD, out.type);
    ASSERT_FLOAT_NEAR(8.0f, out.intensity, 0.001f);

    /* Sample far away → should be zero. */
    hit = npc_scent_sample(&f, 20.0f, 20.0f, 20.0f, &out);
    ASSERT_TRUE(!hit);
    ASSERT_FLOAT_NEAR(0.0f, out.intensity, 0.001f);

    npc_scent_field_destroy(&f);
    PASS();
}

/* 7. Multiple scent types do not clobber each other. */
static void test_multiple_scent_types(void)
{
    npc_scent_field_t f;
    npc_scent_field_init(&f, 32, 1.0f);

    /* Emit BLOOD and FOOD at the same cell. */
    npc_scent_emitter_t em1;
    em1.pos[0]   = 5.0f;
    em1.pos[1]   = 5.0f;
    em1.pos[2]   = 5.0f;
    em1.type     = NPC_SCENT_BLOOD;
    em1.intensity = 3.0f;
    em1.remaining_ticks = 1;

    npc_scent_emitter_t em2;
    em2.pos[0]   = 5.0f;
    em2.pos[1]   = 5.0f;
    em2.pos[2]   = 5.0f;
    em2.type     = NPC_SCENT_FOOD;
    em2.intensity = 7.0f;
    em2.remaining_ticks = 1;

    npc_scent_emit(&f, &em1);
    npc_scent_emit(&f, &em2);

    /* Verify both types at cell. */
    int off = (5 * 32 * 32 + 5 * 32 + 5) * NPC_SCENT_MAX_TYPES;
    ASSERT_FLOAT_NEAR(3.0f, f.grid[off + NPC_SCENT_BLOOD], 0.001f);
    ASSERT_FLOAT_NEAR(7.0f, f.grid[off + NPC_SCENT_FOOD], 0.001f);

    /* Sample should return FOOD (higher intensity). */
    npc_scent_sample_t out;
    bool hit = npc_scent_sample(&f, 5.0f, 5.0f, 5.0f, &out);
    ASSERT_TRUE(hit);
    ASSERT_INT_EQ(NPC_SCENT_FOOD, out.type);
    ASSERT_FLOAT_NEAR(7.0f, out.intensity, 0.001f);

    npc_scent_field_destroy(&f);
    PASS();
}

/* 8. Scent decays to zero after repeated advection. */
static void test_scent_decay_to_zero(void)
{
    npc_scent_field_t f;
    npc_scent_field_init(&f, 32, 1.0f);

    f.wind[0] = 1.0f; /* blow east */

    npc_scent_emitter_t em;
    em.pos[0]   = 5.0f;
    em.pos[1]   = 10.0f;
    em.pos[2]   = 10.0f;
    em.type     = NPC_SCENT_SMOKE;
    em.intensity = 100.0f;
    em.remaining_ticks = 0;

    npc_scent_emit(&f, &em);

    /* Advect many times — scent shifts downwind and decays. */
    int i;
    for (i = 0; i < 200; i++)
        npc_scent_advect(&f, 1.0f);

    /* Eventually scent should be near zero everywhere. */
    npc_scent_sample_t out;
    bool hit = npc_scent_sample(&f, 5.0f, 10.0f, 10.0f, &out);
    /* Either no hit or near-zero. */
    if (hit)
        ASSERT_TRUE(out.intensity < 0.001f);

    npc_scent_field_destroy(&f);
    PASS();
}

/* 9. Advect with no-wind does not move scent. */
static void test_advect_no_wind(void)
{
    npc_scent_field_t f;
    npc_scent_field_init(&f, 32, 1.0f);

    /* Wind is zero. */
    npc_scent_emitter_t em;
    em.pos[0]   = 5.0f;
    em.pos[1]   = 5.0f;
    em.pos[2]   = 5.0f;
    em.type     = NPC_SCENT_SWEAT;
    em.intensity = 5.0f;
    em.remaining_ticks = 0;

    npc_scent_emit(&f, &em);

    /* Advect twice — scent stays (damped in place). */
    npc_scent_advect(&f, 1.0f);
    npc_scent_advect(&f, 1.0f);

    npc_scent_sample_t out;
    bool hit = npc_scent_sample(&f, 5.0f, 5.0f, 5.0f, &out);
    ASSERT_TRUE(hit);
    ASSERT_INT_EQ(NPC_SCENT_SWEAT, out.type);
    /* Damped twice: 5.0 * 0.95 * 0.95 */
    ASSERT_FLOAT_NEAR(5.0f * 0.95f * 0.95f, out.intensity, 0.01f);

    npc_scent_field_destroy(&f);
    PASS();
}

/* ---- Main ------------------------------------------------------- */

int main(void)
{
    printf("npc_scent_tests\n");
    RUN(test_init_destroy);
    RUN(test_destroy_null);
    RUN(test_emit_scent_at_cell);
    RUN(test_emit_clamp_bounds);
    RUN(test_advect_shifts_scent);
    RUN(test_sample_correct_concentration);
    RUN(test_multiple_scent_types);
    RUN(test_scent_decay_to_zero);
    RUN(test_advect_no_wind);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
