#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/job/system.h"
#include "ferrum/server/physics/sync/pre_physics_sync.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((int)(exp) != (int)(act)) {                                                                  \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,  \
                    (int)(exp), (int)(act));                                                              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                                                 \
    do {                                                                                                 \
        float _d = (float)(exp) - (float)(act);                                                          \
        if (_d < -(eps) || _d > (eps)) {                                                                 \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f got %f\n",                   \
                    __FILE__, __LINE__, (double)(exp), (double)(act));                                    \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

/* ── Helper: make a pool with N dynamic bodies ──────────────────── */

static int make_pool_(phys_body_pool_t *pool, uint32_t count) {
    if (phys_body_pool_init(pool, count) != 0) {
        return -1;
    }
    for (uint32_t i = 0; i < count; ++i) {
        phys_body_t b;
        phys_body_init(&b);
        phys_body_set_mass(&b, 1.0f);
        b.position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        b.linear_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        uint32_t idx = 0;
        if (phys_body_pool_add(pool, &b, &idx) != 0) {
            return -1;
        }
    }
    return 0;
}

/* ── Happy path: dirty records write velocity + position ─────────── */

static int test_sync_writes_dirty_records(void) {
    phys_body_pool_t pool;
    ASSERT_INT_EQ(0, make_pool_(&pool, 4));

    phys_sync_record_t records[2];
    memset(records, 0, sizeof(records));

    /* Record 0: dirty, body 1, entity 10, velocity (1,2,3), position (4,5,6). */
    records[0].body_index = 1;
    records[0].entity_index = 10;
    records[0].linear_vel = (phys_vec3_t){1.0f, 2.0f, 3.0f};
    records[0].position = (phys_vec3_t){4.0f, 5.0f, 6.0f};
    records[0].dirty = 1;

    /* Record 1: dirty, body 3, entity 20. */
    records[1].body_index = 3;
    records[1].entity_index = 20;
    records[1].linear_vel = (phys_vec3_t){7.0f, 8.0f, 9.0f};
    records[1].position = (phys_vec3_t){10.0f, 11.0f, 12.0f};
    records[1].dirty = 1;

    phys_pre_physics_sync_args_t args = {
        .records = records,
        .record_count = 2,
        .body_pool = &pool,
    };
    ASSERT_INT_EQ(0, phys_pre_physics_sync(&args));

    /* Verify bodies_next was written. */
    phys_body_t *b1 = phys_body_pool_get_next(&pool, 1);
    ASSERT_TRUE(b1 != NULL);
    ASSERT_FLOAT_NEAR(1.0f, b1->linear_vel.x, 1e-6f);
    ASSERT_FLOAT_NEAR(2.0f, b1->linear_vel.y, 1e-6f);
    ASSERT_FLOAT_NEAR(3.0f, b1->linear_vel.z, 1e-6f);
    ASSERT_FLOAT_NEAR(4.0f, b1->position.x, 1e-6f);
    ASSERT_FLOAT_NEAR(5.0f, b1->position.y, 1e-6f);
    ASSERT_FLOAT_NEAR(6.0f, b1->position.z, 1e-6f);
    ASSERT_INT_EQ(10, (int)b1->entity_index);

    phys_body_t *b3 = phys_body_pool_get_next(&pool, 3);
    ASSERT_TRUE(b3 != NULL);
    ASSERT_FLOAT_NEAR(7.0f, b3->linear_vel.x, 1e-6f);
    ASSERT_FLOAT_NEAR(10.0f, b3->position.x, 1e-6f);
    ASSERT_INT_EQ(20, (int)b3->entity_index);

    /* Verify bodies_curr was NOT modified. */
    phys_body_t *b1c = phys_body_pool_get_curr(&pool, 1);
    ASSERT_TRUE(b1c != NULL);
    ASSERT_FLOAT_NEAR(0.0f, b1c->linear_vel.x, 1e-6f);

    phys_body_pool_destroy(&pool);
    return 0;
}

/* ── Edge: non-dirty records are skipped ────────────────────────── */

static int test_sync_skips_non_dirty(void) {
    phys_body_pool_t pool;
    ASSERT_INT_EQ(0, make_pool_(&pool, 2));

    phys_sync_record_t records[2];
    memset(records, 0, sizeof(records));

    /* Record 0: NOT dirty. */
    records[0].body_index = 0;
    records[0].entity_index = 5;
    records[0].linear_vel = (phys_vec3_t){99.0f, 0.0f, 0.0f};
    records[0].dirty = 0;

    /* Record 1: dirty. */
    records[1].body_index = 1;
    records[1].entity_index = 6;
    records[1].linear_vel = (phys_vec3_t){42.0f, 0.0f, 0.0f};
    records[1].dirty = 1;

    phys_pre_physics_sync_args_t args = {
        .records = records,
        .record_count = 2,
        .body_pool = &pool,
    };
    ASSERT_INT_EQ(0, phys_pre_physics_sync(&args));

    /* Body 0: should be untouched. */
    phys_body_t *b0 = phys_body_pool_get_next(&pool, 0);
    ASSERT_TRUE(b0 != NULL);
    ASSERT_FLOAT_NEAR(0.0f, b0->linear_vel.x, 1e-6f);
    ASSERT_INT_EQ((int)UINT32_MAX, (int)b0->entity_index);

    /* Body 1: should have been written. */
    phys_body_t *b1 = phys_body_pool_get_next(&pool, 1);
    ASSERT_TRUE(b1 != NULL);
    ASSERT_FLOAT_NEAR(42.0f, b1->linear_vel.x, 1e-6f);
    ASSERT_INT_EQ(6, (int)b1->entity_index);

    phys_body_pool_destroy(&pool);
    return 0;
}

/* ── Edge: out-of-range body_index is silently skipped ──────────── */

static int test_sync_skips_out_of_range_body(void) {
    phys_body_pool_t pool;
    ASSERT_INT_EQ(0, make_pool_(&pool, 2));

    phys_sync_record_t records[1];
    memset(records, 0, sizeof(records));
    records[0].body_index = 999; /* way beyond pool capacity */
    records[0].entity_index = 1;
    records[0].linear_vel = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    records[0].dirty = 1;

    phys_pre_physics_sync_args_t args = {
        .records = records,
        .record_count = 1,
        .body_pool = &pool,
    };
    /* Should succeed without crashing. */
    ASSERT_INT_EQ(0, phys_pre_physics_sync(&args));

    phys_body_pool_destroy(&pool);
    return 0;
}

/* ── Fail: NULL args returns -1 ──────────────────────────────────── */

static int test_sync_null_args(void) {
    ASSERT_INT_EQ(-1, phys_pre_physics_sync(NULL));

    phys_body_pool_t pool;
    ASSERT_INT_EQ(0, make_pool_(&pool, 1));

    /* NULL records with non-zero count. */
    phys_pre_physics_sync_args_t args = {
        .records = NULL,
        .record_count = 1,
        .body_pool = &pool,
    };
    ASSERT_INT_EQ(-1, phys_pre_physics_sync(&args));

    /* NULL pool. */
    phys_sync_record_t rec;
    memset(&rec, 0, sizeof(rec));
    args.records = &rec;
    args.body_pool = NULL;
    ASSERT_INT_EQ(-1, phys_pre_physics_sync(&args));

    phys_body_pool_destroy(&pool);
    return 0;
}

/* ── Edge: zero record_count is a no-op success ──────────────────── */

static int test_sync_zero_records(void) {
    phys_body_pool_t pool;
    ASSERT_INT_EQ(0, make_pool_(&pool, 1));

    phys_pre_physics_sync_args_t args = {
        .records = NULL,
        .record_count = 0,
        .body_pool = &pool,
    };
    ASSERT_INT_EQ(0, phys_pre_physics_sync(&args));

    phys_body_pool_destroy(&pool);
    return 0;
}

/* ── Parallel: same results as sequential ───────────────────────── */

static int test_sync_parallel_matches_sequential(void) {
    phys_body_pool_t pool_seq;
    phys_body_pool_t pool_par;
    ASSERT_INT_EQ(0, make_pool_(&pool_seq, 8));
    ASSERT_INT_EQ(0, make_pool_(&pool_par, 8));

    /* Build records: some dirty, some not. */
    phys_sync_record_t records[8];
    memset(records, 0, sizeof(records));
    for (uint32_t i = 0; i < 8; ++i) {
        records[i].body_index = i;
        records[i].entity_index = 100 + i;
        records[i].linear_vel = (phys_vec3_t){(float)i, 0.0f, 0.0f};
        records[i].position = (phys_vec3_t){0.0f, (float)i, 0.0f};
        records[i].dirty = (i % 2 == 0) ? 1 : 0;
    }

    /* Sequential pass. */
    phys_pre_physics_sync_args_t seq_args = {
        .records = records,
        .record_count = 8,
        .body_pool = &pool_seq,
    };
    ASSERT_INT_EQ(0, phys_pre_physics_sync(&seq_args));

    /* Parallel pass with very small batch size to force multiple jobs. */
    job_system_t jobs;
    job_system_create_status_t st = job_system_create(&jobs, 2, 64, 64 * 1024, 64, 0);
    ASSERT_INT_EQ(JOB_CREATE_OK, (int)st);
    ASSERT_INT_EQ(0, job_system_start(&jobs));

    phys_pre_physics_sync_par_args_t par_args = {
        .records = records,
        .record_count = 8,
        .body_pool = &pool_par,
        .jobs = &jobs,
        .batch_size = 2, /* 4 batches of 2 */
    };
    ASSERT_INT_EQ(0, phys_pre_physics_sync_par(&par_args));

    /* Compare results. */
    for (uint32_t i = 0; i < 8; ++i) {
        phys_body_t *bs = phys_body_pool_get_next(&pool_seq, i);
        phys_body_t *bp = phys_body_pool_get_next(&pool_par, i);
        ASSERT_TRUE(bs != NULL);
        ASSERT_TRUE(bp != NULL);
        ASSERT_FLOAT_NEAR(bs->linear_vel.x, bp->linear_vel.x, 1e-6f);
        ASSERT_FLOAT_NEAR(bs->position.y, bp->position.y, 1e-6f);
        ASSERT_INT_EQ((int)bs->entity_index, (int)bp->entity_index);
    }

    job_system_shutdown(&jobs);
    phys_body_pool_destroy(&pool_seq);
    phys_body_pool_destroy(&pool_par);
    return 0;
}

/* ── Test runner ─────────────────────────────────────────────────── */

#define RUN_TEST(fn)                                                                                     \
    do {                                                                                                 \
        printf("  %-60s", #fn);                                                                          \
        int _r = fn();                                                                                   \
        printf("%s\n", _r ? "FAIL" : "PASS");                                                        \
        if (_r) fail_count++;                                                                            \
        test_count++;                                                                                    \
    } while (0)

int main(void) {
    int fail_count = 0;
    int test_count = 0;

    printf("p092_server_pre_physics_sync_tests:\n");

    RUN_TEST(test_sync_writes_dirty_records);
    RUN_TEST(test_sync_skips_non_dirty);
    RUN_TEST(test_sync_skips_out_of_range_body);
    RUN_TEST(test_sync_null_args);
    RUN_TEST(test_sync_zero_records);
    RUN_TEST(test_sync_parallel_matches_sequential);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
