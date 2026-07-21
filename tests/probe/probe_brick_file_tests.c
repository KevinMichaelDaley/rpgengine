/**
 * @file probe_brick_file_tests.c
 * @brief Unit tests for the .bricks sidecar IO (rpg-pjkb, feature 5).
 *        Written before the implementation (TDD phase 1).
 *
 * The forward pass samples probes through the brick index (fragment -> voxel
 * -> brick -> 8 of its 64 probes): the offline pass must therefore ship the
 * BRICKS (with per-probe validity) alongside the .probes positions, and the
 * loader rebuilds the dense voxel index from them (fast, already tested).
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/place/probe_brick.h"
#include "ferrum/probe/place/probe_brick_file.h"

#define ASSERT_TRUE(e) do { if (!(e)) { fprintf(stderr, \
    "  ASSERT_TRUE failed: %s (%s:%d)\n", #e, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_FALSE(e) ASSERT_TRUE(!(e))
#define ASSERT_INT_EQ(a,b) do { long _a=(long)(a),_b=(long)(b); if (_a!=_b) { \
    fprintf(stderr,"  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)

#define TMP_PATH "/tmp/probe_brick_file_test.bricks"

static uint8_t g_buf[4 * 1024 * 1024];

/* Build a tiny deterministic data set by hand. */
static void fill_data(probe_brick_data_t *d, probe_brick_t *bricks,
                      uint8_t *valid)
{
    memset(d, 0, sizeof *d);
    memset(bricks, 0, 2 * sizeof *bricks);
    bricks[0].size = 9.0f; bricks[0].level = 0;
    bricks[1].min[0] = 3.0f; bricks[1].size = 3.0f; bricks[1].level = 1;
    for (int i = 0; i < 64; ++i) {
        bricks[0].probe_idx[i] = (uint32_t)i;
        bricks[1].probe_idx[i] = (uint32_t)(i % 7);
    }
    valid[0] = 1; valid[1] = 0; valid[2] = 1; valid[3] = 1;
    valid[4] = 0; valid[5] = 1; valid[6] = 1;
    d->bricks = bricks; d->n_bricks = 2;
    d->valid = valid; d->n_probes = 7;
    d->coarse_brick = 9.0f; d->levels = 2;
    d->aabb_min[0] = 0; d->aabb_min[1] = 0; d->aabb_min[2] = 0;
    d->aabb_max[0] = 9; d->aabb_max[1] = 9; d->aabb_max[2] = 9;
}

static int test_roundtrip(void)
{
    probe_brick_t bricks[2]; uint8_t valid[7];
    probe_brick_data_t d; fill_data(&d, bricks, valid);
    ASSERT_TRUE(probe_brick_data_save(TMP_PATH, &d));

    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_data_t l;
    ASSERT_TRUE(probe_brick_data_load(TMP_PATH, &a, &l));
    ASSERT_INT_EQ(l.n_bricks, 2);
    ASSERT_INT_EQ(l.n_probes, 7);
    ASSERT_INT_EQ(l.levels, 2);
    ASSERT_TRUE(l.coarse_brick == 9.0f);
    ASSERT_TRUE(l.aabb_max[0] == 9.0f);
    ASSERT_TRUE(memcmp(l.bricks, bricks, sizeof bricks) == 0);
    ASSERT_TRUE(memcmp(l.valid, valid, sizeof valid) == 0);
    return 0;
}

static int test_load_failures(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_brick_data_t l;
    ASSERT_FALSE(probe_brick_data_load("/tmp/definitely_missing.bricks", &a, &l));
    /* Corrupt magic. */
    FILE *f = fopen(TMP_PATH, "wb");
    ASSERT_TRUE(f != NULL);
    fwrite("NOPE", 1, 4, f); fclose(f);
    ASSERT_FALSE(probe_brick_data_load(TMP_PATH, &a, &l));
    /* NULLs. */
    ASSERT_FALSE(probe_brick_data_load(NULL, &a, &l));
    ASSERT_FALSE(probe_brick_data_load(TMP_PATH, NULL, &l));
    ASSERT_FALSE(probe_brick_data_load(TMP_PATH, &a, NULL));
    return 0;
}

static int test_save_failures(void)
{
    probe_brick_t bricks[2]; uint8_t valid[7];
    probe_brick_data_t d; fill_data(&d, bricks, valid);
    ASSERT_FALSE(probe_brick_data_save(NULL, &d));
    ASSERT_FALSE(probe_brick_data_save("/definitely/missing/dir/x.bricks", &d));
    d.bricks = NULL;
    ASSERT_FALSE(probe_brick_data_save(TMP_PATH, &d));
    return 0;
}

/* Tiny arena: load fails cleanly on exhaustion. */
static int test_arena_exhaustion(void)
{
    probe_brick_t bricks[2]; uint8_t valid[7];
    probe_brick_data_t d; fill_data(&d, bricks, valid);
    ASSERT_TRUE(probe_brick_data_save(TMP_PATH, &d));
    static uint8_t tiny[64];
    arena_t t; arena_init(&t, tiny, sizeof tiny);
    probe_brick_data_t l;
    ASSERT_FALSE(probe_brick_data_load(TMP_PATH, &t, &l));
    return 0;
}

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_case_t;

int main(void)
{
    static const test_case_t tests[] = {
        { "roundtrip",        test_roundtrip },
        { "load_failures",    test_load_failures },
        { "save_failures",    test_save_failures },
        { "arena_exhaustion", test_arena_exhaustion },
    };
    int failed = 0;
    const int n = (int)(sizeof tests / sizeof tests[0]);
    for (int i = 0; i < n; ++i) {
        int rc = tests[i].fn();
        if (rc != 0) { fprintf(stderr, "FAIL %s\n", tests[i].name); ++failed; }
    }
    printf("probe_brick_file_tests: %d/%d passed\n", n - failed, n);
    return failed == 0 ? 0 : 1;
}
