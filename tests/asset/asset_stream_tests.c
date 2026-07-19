/**
 * @file asset_stream_tests.c
 * @brief Unit + integration tests for the prioritized asset streaming manager
 *        and the chunk table (rpg-nbp2). Covers priority admission within RAM +
 *        VRAM budgets, priority-raise promotion, lowest-priority-first eviction,
 *        the VRAM tier (upload frees RAM) vs. headless RAM-only, off-main-thread
 *        async loading via the job system, mixed asset classes under one model,
 *        removal, and chunk-residency-gated probe placement end-to-end.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#include "ferrum/memory/arena.h"
#include "ferrum/job/system.h"
#include "ferrum/asset/asset_stream.h"
#include "ferrum/asset/chunk_table.h"
#include "ferrum/scene/scene_desc_probes.h"
#include "ferrum/probe/probe_set.h"
#include "ferrum/probe/probe_place.h"

#define ASSERT_TRUE(e) do { if (!(e)) { fprintf(stderr, \
    "  ASSERT_TRUE failed: %s (%s:%d)\n", #e, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_FALSE(e) ASSERT_TRUE(!(e))
#define ASSERT_INT_EQ(a,b) do { long _a=(long)(a),_b=(long)(b); if (_a!=_b) { \
    fprintf(stderr,"  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)

/* Per-asset test record the callbacks operate on. */
typedef struct trec {
    size_t size;       /**< RAM bytes this asset "decodes" to. */
    size_t vram;       /**< VRAM bytes on upload (0 = CPU-only). */
    int    loads;
    int    uploads;
    int    evicts_ram;
    int    evicts_vram;
    int    off_thread; /**< set if load ran off the main thread. */
} trec_t;

static pthread_t g_main;

static size_t cb_load(void *user, uint64_t id, fr_asset_class_t cls, void *su)
{
    (void)user; (void)id; (void)cls;
    trec_t *t = (trec_t *)su;
    if (!pthread_equal(pthread_self(), g_main)) t->off_thread = 1;
    t->loads++;
    return t->size;
}
static size_t cb_upload(void *user, uint64_t id, fr_asset_class_t cls, void *su)
{
    (void)user; (void)id; (void)cls;
    trec_t *t = (trec_t *)su;
    t->uploads++;
    return t->vram;
}
static void cb_evict(void *user, uint64_t id, fr_asset_class_t cls, void *su, int drop)
{
    (void)user; (void)id; (void)cls;
    trec_t *t = (trec_t *)su;
    if (drop & FR_ASSET_DROP_RAM) t->evicts_ram++;
    if (drop & FR_ASSET_DROP_VRAM) t->evicts_vram++;
}

static fr_asset_stream_cbs_t make_cbs(void)
{
    fr_asset_stream_cbs_t c; memset(&c, 0, sizeof c);
    c.load = cb_load; c.upload = cb_upload; c.evict = cb_evict;
    return c;
}

/* Synchronous manager (jobs=NULL) with a RAM budget of `ram` bytes. */
static void init_sync(fr_asset_stream_t *s, size_t ram, size_t vram)
{
    fr_asset_stream_config_t cfg; memset(&cfg, 0, sizeof cfg);
    cfg.jobs = NULL; cfg.ram_budget = ram; cfg.vram_budget = vram;
    cfg.max_in_flight = 8; cfg.capacity = 32; cfg.cbs = make_cbs();
    fr_asset_stream_init(s, &cfg);
}

static int test_priority_admission_within_budget(void)
{
    fr_asset_stream_t s; init_sync(&s, 3000, 0);
    static trec_t r[5]; memset(r, 0, sizeof r);
    int pri[5] = { 10, 20, 30, 40, 50 };
    for (int i = 0; i < 5; ++i) {
        r[i].size = 1000;
        ASSERT_TRUE(fr_asset_stream_add(&s, (uint64_t)(i + 1), FR_ASSET_MESH,
                                        1000, 0, pri[i], &r[i]));
    }
    fr_asset_stream_tick(&s);
    /* Budget fits 3: the three highest-priority (50,40,30) are resident. */
    ASSERT_INT_EQ(fr_asset_stream_resident_count(&s), 3);
    ASSERT_INT_EQ(fr_asset_stream_ram_used(&s), 3000);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 5), FR_RESIDENCY_RAM); /* pri 50 */
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 4), FR_RESIDENCY_RAM); /* pri 40 */
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 3), FR_RESIDENCY_RAM); /* pri 30 */
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 2), FR_RESIDENCY_ABSENT); /* pri 20 */
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 1), FR_RESIDENCY_ABSENT); /* pri 10 */
    fr_asset_stream_destroy(&s);
    return 0;
}

static int test_raise_priority_promotes(void)
{
    fr_asset_stream_t s; init_sync(&s, 2000, 0);
    static trec_t ra, rb, rc; memset(&ra, 0, sizeof ra); rb = ra; rc = ra;
    ra.size = rb.size = rc.size = 1000;
    fr_asset_stream_add(&s, 1, FR_ASSET_MESH, 1000, 0, 10, &ra);
    fr_asset_stream_add(&s, 2, FR_ASSET_MESH, 1000, 0, 20, &rb);
    fr_asset_stream_tick(&s);
    ASSERT_INT_EQ(fr_asset_stream_resident_count(&s), 2); /* A,B resident */

    fr_asset_stream_add(&s, 3, FR_ASSET_MESH, 1000, 0, 5, &rc); /* below both */
    fr_asset_stream_tick(&s);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 3), FR_RESIDENCY_ABSENT); /* no room */

    fr_asset_stream_set_priority(&s, 3, 30); /* now highest */
    fr_asset_stream_tick(&s);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 3), FR_RESIDENCY_RAM);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 1), FR_RESIDENCY_ABSENT); /* A evicted (lowest) */
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 2), FR_RESIDENCY_RAM);    /* B kept */
    ASSERT_INT_EQ(ra.evicts_ram, 1);
    fr_asset_stream_destroy(&s);
    return 0;
}

static int test_eviction_lowest_first(void)
{
    fr_asset_stream_t s; init_sync(&s, 3000, 0);
    static trec_t r[4]; memset(r, 0, sizeof r);
    int pri[4] = { 10, 20, 30, 40 };
    for (int i = 0; i < 3; ++i) { r[i].size = 1000;
        fr_asset_stream_add(&s, (uint64_t)(i + 1), FR_ASSET_MESH, 1000, 0, pri[i], &r[i]); }
    fr_asset_stream_tick(&s);
    ASSERT_INT_EQ(fr_asset_stream_resident_count(&s), 3);
    /* Add a higher-priority asset -> the lowest-priority resident (pri 10) evicts. */
    r[3].size = 1000;
    fr_asset_stream_add(&s, 4, FR_ASSET_MESH, 1000, 0, pri[3], &r[3]);
    fr_asset_stream_tick(&s);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 1), FR_RESIDENCY_ABSENT); /* pri 10 gone */
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 4), FR_RESIDENCY_RAM);    /* pri 40 in */
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 2), FR_RESIDENCY_RAM);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 3), FR_RESIDENCY_RAM);
    fr_asset_stream_destroy(&s);
    return 0;
}

static int test_vram_tier_and_headless(void)
{
    /* VRAM tier: assets go RAM then VRAM; the RAM decode is freed after upload. */
    fr_asset_stream_t s; init_sync(&s, 100000, 2000);
    static trec_t r[2]; memset(r, 0, sizeof r);
    for (int i = 0; i < 2; ++i) { r[i].size = 1000; r[i].vram = 1000;
        fr_asset_stream_add(&s, (uint64_t)(i + 1), FR_ASSET_TEXTURE, 1000, 1000, 10 + i, &r[i]); }
    fr_asset_stream_tick(&s);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 1), FR_RESIDENCY_VRAM);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 2), FR_RESIDENCY_VRAM);
    ASSERT_INT_EQ(fr_asset_stream_vram_used(&s), 2000);
    ASSERT_INT_EQ(fr_asset_stream_ram_used(&s), 0);   /* transient RAM freed */
    ASSERT_INT_EQ(r[0].uploads, 1);
    ASSERT_INT_EQ(r[0].evicts_ram, 1);                /* RAM freed after upload */
    fr_asset_stream_destroy(&s);

    /* Headless: vram_budget 0 -> no upload, assets stay RAM-resident. */
    fr_asset_stream_t h; init_sync(&h, 100000, 0);
    static trec_t hr; memset(&hr, 0, sizeof hr); hr.size = 1000; hr.vram = 1000;
    fr_asset_stream_add(&h, 1, FR_ASSET_TEXTURE, 1000, 1000, 10, &hr);
    fr_asset_stream_tick(&h);
    ASSERT_INT_EQ(fr_asset_stream_residency(&h, 1), FR_RESIDENCY_RAM);
    ASSERT_INT_EQ(hr.uploads, 0);
    fr_asset_stream_destroy(&h);
    return 0;
}

static int test_async_offthread(void)
{
    job_system_t js; memset(&js, 0, sizeof js);
    ASSERT_TRUE(job_system_create(&js, 2, 2048, 64 * 1024, 256, 0) == JOB_CREATE_OK);
    ASSERT_TRUE(job_system_start(&js) == 0);

    fr_asset_stream_config_t cfg; memset(&cfg, 0, sizeof cfg);
    cfg.jobs = &js; cfg.ram_budget = 0 /*unlimited*/; cfg.vram_budget = 0;
    cfg.max_in_flight = 4; cfg.capacity = 16; cfg.cbs = make_cbs();
    fr_asset_stream_t s; ASSERT_TRUE(fr_asset_stream_init(&s, &cfg));

    static trec_t r[6]; memset(r, 0, sizeof r);
    for (int i = 0; i < 6; ++i) { r[i].size = 1000;
        fr_asset_stream_add(&s, (uint64_t)(i + 1), FR_ASSET_MESH, 1000, 0, i, &r[i]); }

    /* Dispatch loads, let the workers run, harvest. */
    for (int spin = 0; spin < 100 && fr_asset_stream_resident_count(&s) < 6; ++spin) {
        fr_asset_stream_tick(&s);
        job_system_wait_idle(&js);   /* loads run on worker fibers, not here */
    }
    ASSERT_INT_EQ(fr_asset_stream_resident_count(&s), 6);
    int off = 0;
    for (int i = 0; i < 6; ++i) off += r[i].off_thread;
    ASSERT_TRUE(off > 0);   /* at least one load executed off the main thread */

    fr_asset_stream_destroy(&s);
    job_system_shutdown(&js);
    return 0;
}

static int test_mixed_classes_shared_model(void)
{
    /* Meshes, a skeleton, a lightmap chunk and an SDF chunk all page under the
     * one residency model (acceptance: chunks share the model with one-shots). */
    fr_asset_stream_t s; init_sync(&s, 100000, 0);
    static trec_t r[4]; memset(r, 0, sizeof r);
    fr_asset_class_t cls[4] = { FR_ASSET_MESH, FR_ASSET_SKELETON,
                                FR_ASSET_LIGHTMAP_CHUNK, FR_ASSET_SDF_CHUNK };
    for (int i = 0; i < 4; ++i) { r[i].size = 500;
        fr_asset_stream_add(&s, (uint64_t)(i + 1), cls[i], 500, 0, 10, &r[i]); }
    fr_asset_stream_tick(&s);
    ASSERT_INT_EQ(fr_asset_stream_resident_count(&s), 4);
    fr_asset_stream_destroy(&s);
    return 0;
}

static int test_remove(void)
{
    fr_asset_stream_t s; init_sync(&s, 100000, 0);
    static trec_t r; memset(&r, 0, sizeof r); r.size = 1000;
    fr_asset_stream_add(&s, 7, FR_ASSET_MESH, 1000, 0, 10, &r);
    fr_asset_stream_tick(&s);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 7), FR_RESIDENCY_RAM);
    ASSERT_TRUE(fr_asset_stream_remove(&s, 7));
    ASSERT_INT_EQ(fr_asset_stream_ram_used(&s), 0);
    ASSERT_INT_EQ(r.evicts_ram, 1);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 7), FR_RESIDENCY_ABSENT); /* unknown */
    fr_asset_stream_destroy(&s);
    return 0;
}

/* End-to-end: chunk residency (by interest, under budget) gates which probes
 * are generated -- the acceptance "chunk residency drives probes". */
static int in_box(const float *p, const float *lo, const float *hi)
{
    return p[0] >= lo[0] && p[0] <= hi[0] && p[1] >= lo[1] && p[1] <= hi[1] &&
           p[2] >= lo[2] && p[2] <= hi[2];
}

static int test_chunk_gated_probes(void)
{
    /* Budget fits 2 of 3 chunks. */
    fr_asset_stream_t s; init_sync(&s, 2000, 0);
    static fr_chunk_entry_t storage[8];
    fr_chunk_table_t tbl; fr_chunk_table_init(&tbl, &s, storage, 8);

    static trec_t r[3]; memset(r, 0, sizeof r);
    float amin[3][3] = { {0,0,0}, {4,0,0}, {8,0,0} };
    float amax[3][3] = { {2,2,2}, {6,2,2}, {10,2,2} };
    for (int i = 0; i < 3; ++i) { r[i].size = 1000;
        ASSERT_TRUE(fr_chunk_table_add(&tbl, (uint64_t)(i + 1), FR_ASSET_SDF_CHUNK,
                                       amin[i], amax[i], 1000, 0, &r[i])); }

    /* Interest at (1,1,1): chunk 0 nearest, chunk 2 farthest. */
    float interest[3] = { 1, 1, 1 };
    fr_chunk_table_set_interest(&tbl, interest, 1.0f);
    fr_asset_stream_tick(&s);
    /* Two nearest chunks resident, farthest not. */
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 1), FR_RESIDENCY_RAM);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 2), FR_RESIDENCY_RAM);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 3), FR_RESIDENCY_ABSENT);

    float rmin[8 * 3], rmax[8 * 3];
    uint32_t nres = fr_chunk_table_resident_boxes(&tbl, rmin, rmax, 8);
    ASSERT_INT_EQ(nres, 2);

    /* Build a probe grid spanning all three chunks, then gate by resident boxes. */
    arena_t a; static uint8_t buf[4 * 1024 * 1024]; arena_init(&a, buf, sizeof buf);
    scene_desc_probes_t spec; memset(&spec, 0, sizeof spec);
    spec.spacing = 0.5f; spec.vspacing = 0.5f;
    float gmin[3] = { 0, 0, 0 }, gmax[3] = { 10, 2, 2 };
    probe_set_t grid;
    ASSERT_TRUE(probe_place_grid(&spec, gmin, gmax, &a, &grid));
    probe_set_t kept;
    uint32_t nk = probe_place_filter_chunks(&grid, rmin, rmax, nres, &a, &kept);
    ASSERT_TRUE(nk > 0 && nk < grid.count);      /* some kept, some (chunk 2) dropped */
    /* No surviving probe lies in the evicted/absent chunk 2's box. */
    for (uint32_t i = 0; i < kept.count; ++i)
        ASSERT_FALSE(in_box(&kept.positions[i * 3], amin[2], amax[2]));
    fr_asset_stream_destroy(&s);
    return 0;
}

int main(void)
{
    g_main = pthread_self();
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"priority_admission_within_budget", test_priority_admission_within_budget},
        {"raise_priority_promotes",          test_raise_priority_promotes},
        {"eviction_lowest_first",            test_eviction_lowest_first},
        {"vram_tier_and_headless",           test_vram_tier_and_headless},
        {"async_offthread",                  test_async_offthread},
        {"mixed_classes_shared_model",       test_mixed_classes_shared_model},
        {"remove",                           test_remove},
        {"chunk_gated_probes",               test_chunk_gated_probes},
    };
    int n = (int)(sizeof tests / sizeof tests[0]), fails = 0;
    for (int i = 0; i < n; ++i) {
        int r = tests[i].fn();
        fprintf(stderr, "[%s] %s\n", r ? "FAIL" : "ok  ", tests[i].name);
        fails += (r != 0);
    }
    fprintf(stderr, "\nasset_stream_tests: %d/%d passed\n", n - fails, n);
    return fails ? 1 : 0;
}
