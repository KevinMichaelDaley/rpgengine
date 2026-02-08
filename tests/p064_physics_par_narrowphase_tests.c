/**
 * @file p064_physics_par_narrowphase_tests.c
 * @brief Tests for parallel narrowphase dispatch (phys-305).
 *
 * Validates that phys_stage_narrowphase_par() produces the same
 * contact candidates as the sequential phys_stage_narrowphase(),
 * handles edge cases (zero pairs, no contacts, buffer limits),
 * and works correctly with mixed shape types.
 */

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/job/system.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/narrowphase.h"
#include "ferrum/physics/par/narrowphase_par.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define TEST_FAIL(msg, ...)                                                    \
    do {                                                                        \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__,           \
                ##__VA_ARGS__);                                                \
        return 1;                                                              \
    } while (0)

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                         \
            TEST_FAIL("%s", #cond);                                            \
        }                                                                       \
    } while (0)

#define ASSERT_EQ_UINT(expected, actual)                                       \
    do {                                                                        \
        unsigned long long _exp = (unsigned long long)(expected);               \
        unsigned long long _act = (unsigned long long)(actual);                 \
        if (_exp != _act) {                                                    \
            TEST_FAIL("expected %llu got %llu", _exp, _act);                   \
        }                                                                       \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const float EPSILON = 1e-4f;

static float fabsf_local(float x) { return x < 0 ? -x : x; }

/* ── Helper: compare candidate sets (order-independent) ────────── */

/**
 * @brief Find a matching candidate in arr by body pair.
 * @return Index in arr, or count if not found.
 */
static uint32_t find_candidate(const phys_contact_candidate_t *arr,
                               uint32_t count,
                               uint32_t body_a, uint32_t body_b)
{
    for (uint32_t i = 0; i < count; i++) {
        if (arr[i].body_a == body_a && arr[i].body_b == body_b) {
            return i;
        }
    }
    return count;
}

/**
 * @brief Check that two candidate sets contain the same contacts
 *        (order-independent).  Compares body pairs, contact counts,
 *        and first-contact normals within EPSILON.
 */
static int candidates_match(const phys_contact_candidate_t *a, uint32_t ca,
                             const phys_contact_candidate_t *b, uint32_t cb)
{
    if (ca != cb) return 0;
    for (uint32_t i = 0; i < ca; i++) {
        uint32_t j = find_candidate(b, cb, a[i].body_a, a[i].body_b);
        if (j == cb) return 0;
        if (a[i].contact_count != b[j].contact_count) return 0;
        /* Compare first contact normal. */
        float dx = fabsf_local(a[i].contacts[0].normal.x - b[j].contacts[0].normal.x);
        float dy = fabsf_local(a[i].contacts[0].normal.y - b[j].contacts[0].normal.y);
        float dz = fabsf_local(a[i].contacts[0].normal.z - b[j].contacts[0].normal.z);
        if (dx > EPSILON || dy > EPSILON || dz > EPSILON) return 0;
    }
    return 1;
}

/* ── Helper: set up overlapping sphere bodies ──────────────────── */

/**
 * @brief Create N sphere bodies arranged in pairs along X axis.
 *        Body 2k and 2k+1 form a pair with centers 0.5 apart
 *        (radius=1.0 each, so they overlap).
 *
 * @param n_pairs   Number of pairs to create.
 * @param bodies    Output body array (must hold 2*n_pairs).
 * @param colliders Output collider array (must hold 2*n_pairs).
 * @param spheres   Output sphere pool (must hold at least 1).
 * @param pairs     Output collision pair array (must hold n_pairs).
 */
static void setup_sphere_pairs(uint32_t n_pairs,
                                phys_body_t *bodies,
                                phys_collider_t *colliders,
                                phys_sphere_t *spheres,
                                phys_collision_pair_t *pairs)
{
    /* Single sphere shape shared by all bodies. */
    spheres[0].radius = 1.0f;

    for (uint32_t i = 0; i < n_pairs; i++) {
        uint32_t a = i * 2;
        uint32_t b = i * 2 + 1;

        phys_body_init(&bodies[a]);
        bodies[a].position = (phys_vec3_t){(float)i * 10.0f, 0.0f, 0.0f};
        bodies[a].inv_mass = 1.0f;
        bodies[a].flags = 0;

        phys_body_init(&bodies[b]);
        bodies[b].position = (phys_vec3_t){(float)i * 10.0f + 0.5f, 0.0f, 0.0f};
        bodies[b].inv_mass = 1.0f;
        bodies[b].flags = 0;

        phys_collider_init_sphere(&colliders[a], 0, (phys_vec3_t){0, 0, 0});
        phys_collider_init_sphere(&colliders[b], 0, (phys_vec3_t){0, 0, 0});

        pairs[i].body_a = a;
        pairs[i].body_b = b;
    }
}

/* ── Helper: set up separated sphere bodies (no contacts) ──────── */

static void setup_separated_sphere_pairs(uint32_t n_pairs,
                                          phys_body_t *bodies,
                                          phys_collider_t *colliders,
                                          phys_sphere_t *spheres,
                                          phys_collision_pair_t *pairs)
{
    spheres[0].radius = 1.0f;

    for (uint32_t i = 0; i < n_pairs; i++) {
        uint32_t a = i * 2;
        uint32_t b = i * 2 + 1;

        phys_body_init(&bodies[a]);
        bodies[a].position = (phys_vec3_t){(float)i * 100.0f, 0.0f, 0.0f};
        bodies[a].inv_mass = 1.0f;
        bodies[a].flags = 0;

        phys_body_init(&bodies[b]);
        /* 50 units apart — well beyond r0 + r1 = 2.0. */
        bodies[b].position = (phys_vec3_t){(float)i * 100.0f + 50.0f, 0.0f, 0.0f};
        bodies[b].inv_mass = 1.0f;
        bodies[b].flags = 0;

        phys_collider_init_sphere(&colliders[a], 0, (phys_vec3_t){0, 0, 0});
        phys_collider_init_sphere(&colliders[b], 0, (phys_vec3_t){0, 0, 0});

        pairs[i].body_a = a;
        pairs[i].body_b = b;
    }
}

/* ── Helper: create and tear down job infrastructure ───────────── */

static void make_job_infra(job_system_t *sys, phys_job_context_t *ctx)
{
    job_system_create(sys, 2, 1024, 65536, 256, 0);
    job_system_start(sys);
    phys_job_context_init(ctx, sys);
}

static void tear_job_infra(job_system_t *sys, phys_job_context_t *ctx)
{
    phys_job_context_destroy(ctx);
    job_system_shutdown(sys);
}

/* ── Test 1: par identical to seq with 20 sphere pairs ─────────── */

static int test_par_np_identical_to_seq(void)
{
    enum { N_PAIRS = 20, N_BODIES = N_PAIRS * 2, MAX_CAND = 64 };

    phys_body_t bodies[N_BODIES];
    phys_collider_t colliders[N_BODIES];
    phys_sphere_t spheres[1];
    phys_collision_pair_t pairs[N_PAIRS];
    setup_sphere_pairs(N_PAIRS, bodies, colliders, spheres, pairs);

    /* Sequential run. */
    phys_contact_candidate_t cand_seq[MAX_CAND];
    uint32_t count_seq = 0;
    memset(cand_seq, 0, sizeof(cand_seq));
    phys_narrowphase_args_t args_seq = {
        .bodies = bodies, .colliders = colliders,
        .spheres = spheres, .boxes = NULL, .capsules = NULL,
        .pairs = pairs, .pair_count = N_PAIRS,
        .candidates_out = cand_seq,
        .candidate_count_out = &count_seq,
        .max_candidates = MAX_CAND,
    };
    phys_stage_narrowphase(&args_seq);

    /* Parallel run. */
    phys_contact_candidate_t cand_par[MAX_CAND];
    uint32_t count_par = 0;
    memset(cand_par, 0, sizeof(cand_par));
    phys_narrowphase_args_t args_par = {
        .bodies = bodies, .colliders = colliders,
        .spheres = spheres, .boxes = NULL, .capsules = NULL,
        .pairs = pairs, .pair_count = N_PAIRS,
        .candidates_out = cand_par,
        .candidate_count_out = &count_par,
        .max_candidates = MAX_CAND,
    };

    job_system_t sys;
    phys_job_context_t ctx;
    make_job_infra(&sys, &ctx);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stage_narrowphase_par(&args_par, &ctx, &arena);

    phys_frame_arena_destroy(&arena);
    tear_job_infra(&sys, &ctx);

    ASSERT_EQ_UINT(count_seq, count_par);
    ASSERT_EQ_UINT(N_PAIRS, count_par);
    ASSERT_TRUE(candidates_match(cand_seq, count_seq, cand_par, count_par));

    return 0;
}

/* ── Test 2: 200 pairs → 4 batches ─────────────────────────────── */

static int test_par_np_batch_64(void)
{
    enum { N_PAIRS = 200, N_BODIES = N_PAIRS * 2, MAX_CAND = 256 };

    phys_body_t *bodies = calloc(N_BODIES, sizeof(phys_body_t));
    phys_collider_t *colliders = calloc(N_BODIES, sizeof(phys_collider_t));
    phys_sphere_t spheres[1];
    phys_collision_pair_t *pairs = calloc(N_PAIRS, sizeof(phys_collision_pair_t));
    setup_sphere_pairs(N_PAIRS, bodies, colliders, spheres, pairs);

    /* Sequential. */
    phys_contact_candidate_t *cand_seq = calloc(MAX_CAND, sizeof(phys_contact_candidate_t));
    uint32_t count_seq = 0;
    phys_narrowphase_args_t args_seq = {
        .bodies = bodies, .colliders = colliders,
        .spheres = spheres, .boxes = NULL, .capsules = NULL,
        .pairs = pairs, .pair_count = N_PAIRS,
        .candidates_out = cand_seq,
        .candidate_count_out = &count_seq,
        .max_candidates = MAX_CAND,
    };
    phys_stage_narrowphase(&args_seq);

    /* Parallel. */
    phys_contact_candidate_t *cand_par = calloc(MAX_CAND, sizeof(phys_contact_candidate_t));
    uint32_t count_par = 0;
    phys_narrowphase_args_t args_par = {
        .bodies = bodies, .colliders = colliders,
        .spheres = spheres, .boxes = NULL, .capsules = NULL,
        .pairs = pairs, .pair_count = N_PAIRS,
        .candidates_out = cand_par,
        .candidate_count_out = &count_par,
        .max_candidates = MAX_CAND,
    };

    job_system_t sys;
    phys_job_context_t ctx;
    make_job_infra(&sys, &ctx);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stage_narrowphase_par(&args_par, &ctx, &arena);

    phys_frame_arena_destroy(&arena);
    tear_job_infra(&sys, &ctx);

    ASSERT_EQ_UINT(count_seq, count_par);
    ASSERT_EQ_UINT(N_PAIRS, count_par);
    ASSERT_TRUE(candidates_match(cand_seq, count_seq, cand_par, count_par));

    free(bodies);
    free(colliders);
    free(pairs);
    free(cand_seq);
    free(cand_par);
    return 0;
}

/* ── Test 3: zero pairs → no crash ─────────────────────────────── */

static int test_par_np_zero_pairs(void)
{
    phys_body_t bodies[1];
    phys_collider_t colliders[1];
    phys_sphere_t spheres[1];
    phys_body_init(&bodies[0]);
    spheres[0].radius = 1.0f;
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});

    phys_contact_candidate_t cand[1];
    uint32_t count = 99;
    phys_narrowphase_args_t args = {
        .bodies = bodies, .colliders = colliders,
        .spheres = spheres, .boxes = NULL, .capsules = NULL,
        .pairs = NULL, .pair_count = 0,
        .candidates_out = cand,
        .candidate_count_out = &count,
        .max_candidates = 1,
    };

    job_system_t sys;
    phys_job_context_t ctx;
    make_job_infra(&sys, &ctx);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    /* pair_count=0 but pairs is NULL — should be a safe no-op. */
    phys_stage_narrowphase_par(&args, &ctx, &arena);

    phys_frame_arena_destroy(&arena);
    tear_job_infra(&sys, &ctx);

    /* Sequential treats NULL pairs as no-op, so count stays untouched.
     * But with pair_count=0, the par path sets count to 0. Either way
     * we just want no crash. Accept either 0 or 99. */
    ASSERT_TRUE(count == 0 || count == 99);
    return 0;
}

/* ── Test 4: all pairs separated → 0 candidates ───────────────── */

static int test_par_np_no_contacts(void)
{
    enum { N_PAIRS = 30, N_BODIES = N_PAIRS * 2, MAX_CAND = 64 };

    phys_body_t bodies[N_BODIES];
    phys_collider_t colliders[N_BODIES];
    phys_sphere_t spheres[1];
    phys_collision_pair_t pairs[N_PAIRS];
    setup_separated_sphere_pairs(N_PAIRS, bodies, colliders, spheres, pairs);

    /* Sequential. */
    phys_contact_candidate_t cand_seq[MAX_CAND];
    uint32_t count_seq = 0;
    phys_narrowphase_args_t args_seq = {
        .bodies = bodies, .colliders = colliders,
        .spheres = spheres, .boxes = NULL, .capsules = NULL,
        .pairs = pairs, .pair_count = N_PAIRS,
        .candidates_out = cand_seq,
        .candidate_count_out = &count_seq,
        .max_candidates = MAX_CAND,
    };
    phys_stage_narrowphase(&args_seq);
    ASSERT_EQ_UINT(0, count_seq);

    /* Parallel. */
    phys_contact_candidate_t cand_par[MAX_CAND];
    uint32_t count_par = 0;
    phys_narrowphase_args_t args_par = {
        .bodies = bodies, .colliders = colliders,
        .spheres = spheres, .boxes = NULL, .capsules = NULL,
        .pairs = pairs, .pair_count = N_PAIRS,
        .candidates_out = cand_par,
        .candidate_count_out = &count_par,
        .max_candidates = MAX_CAND,
    };

    job_system_t sys;
    phys_job_context_t ctx;
    make_job_infra(&sys, &ctx);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stage_narrowphase_par(&args_par, &ctx, &arena);

    phys_frame_arena_destroy(&arena);
    tear_job_infra(&sys, &ctx);

    ASSERT_EQ_UINT(0, count_par);
    return 0;
}

/* ── Test 5: mixed shapes (sphere-box, box-box) ────────────────── */

static int test_par_np_mixed_shapes(void)
{
    /*
     * Body 0: sphere (r=1) at origin
     * Body 1: box (he=1,1,1) at (0.5, 0, 0) — overlaps sphere
     * Body 2: box (he=1,1,1) at (1.0, 0, 0) — overlaps body 1
     *
     * Pairs: (0,1) sphere-box, (1,2) box-box
     */
    enum { N_BODIES = 3, N_PAIRS = 2, MAX_CAND = 8 };

    phys_body_t bodies[N_BODIES];
    phys_collider_t colliders[N_BODIES];
    phys_sphere_t spheres[1];
    phys_box_t boxes[1];
    phys_collision_pair_t pairs[N_PAIRS];

    spheres[0].radius = 1.0f;
    boxes[0].half_extents = (phys_vec3_t){1.0f, 1.0f, 1.0f};

    /* Body 0: sphere at origin. */
    phys_body_init(&bodies[0]);
    bodies[0].position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    bodies[0].inv_mass = 1.0f;
    bodies[0].flags = 0;
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});

    /* Body 1: box at (0.5, 0, 0). */
    phys_body_init(&bodies[1]);
    bodies[1].position = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    bodies[1].inv_mass = 1.0f;
    bodies[1].flags = 0;
    phys_collider_init_box(&colliders[1], 0, (phys_vec3_t){0, 0, 0},
                           (phys_quat_t){0, 0, 0, 1});

    /* Body 2: box at (1.0, 0, 0). */
    phys_body_init(&bodies[2]);
    bodies[2].position = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    bodies[2].inv_mass = 1.0f;
    bodies[2].flags = 0;
    phys_collider_init_box(&colliders[2], 0, (phys_vec3_t){0, 0, 0},
                           (phys_quat_t){0, 0, 0, 1});

    pairs[0].body_a = 0;
    pairs[0].body_b = 1;
    pairs[1].body_a = 1;
    pairs[1].body_b = 2;

    /* Sequential. */
    phys_contact_candidate_t cand_seq[MAX_CAND];
    uint32_t count_seq = 0;
    memset(cand_seq, 0, sizeof(cand_seq));
    phys_narrowphase_args_t args_seq = {
        .bodies = bodies, .colliders = colliders,
        .spheres = spheres, .boxes = boxes, .capsules = NULL,
        .pairs = pairs, .pair_count = N_PAIRS,
        .candidates_out = cand_seq,
        .candidate_count_out = &count_seq,
        .max_candidates = MAX_CAND,
    };
    phys_stage_narrowphase(&args_seq);

    /* Parallel. */
    phys_contact_candidate_t cand_par[MAX_CAND];
    uint32_t count_par = 0;
    memset(cand_par, 0, sizeof(cand_par));
    phys_narrowphase_args_t args_par = {
        .bodies = bodies, .colliders = colliders,
        .spheres = spheres, .boxes = boxes, .capsules = NULL,
        .pairs = pairs, .pair_count = N_PAIRS,
        .candidates_out = cand_par,
        .candidate_count_out = &count_par,
        .max_candidates = MAX_CAND,
    };

    job_system_t sys;
    phys_job_context_t ctx;
    make_job_infra(&sys, &ctx);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stage_narrowphase_par(&args_par, &ctx, &arena);

    phys_frame_arena_destroy(&arena);
    tear_job_infra(&sys, &ctx);

    /* Both pairs should produce contacts. */
    ASSERT_EQ_UINT(count_seq, count_par);
    ASSERT_TRUE(count_par >= 2);
    ASSERT_TRUE(candidates_match(cand_seq, count_seq, cand_par, count_par));

    return 0;
}

/* ── Test 6: max candidates buffer limit ───────────────────────── */

static int test_par_np_max_candidates(void)
{
    /* 20 overlapping pairs but only 5 output slots. */
    enum { N_PAIRS = 20, N_BODIES = N_PAIRS * 2, MAX_CAND = 5 };

    phys_body_t bodies[N_BODIES];
    phys_collider_t colliders[N_BODIES];
    phys_sphere_t spheres[1];
    phys_collision_pair_t pairs[N_PAIRS];
    setup_sphere_pairs(N_PAIRS, bodies, colliders, spheres, pairs);

    phys_contact_candidate_t cand_par[MAX_CAND];
    uint32_t count_par = 0;
    memset(cand_par, 0, sizeof(cand_par));
    phys_narrowphase_args_t args_par = {
        .bodies = bodies, .colliders = colliders,
        .spheres = spheres, .boxes = NULL, .capsules = NULL,
        .pairs = pairs, .pair_count = N_PAIRS,
        .candidates_out = cand_par,
        .candidate_count_out = &count_par,
        .max_candidates = MAX_CAND,
    };

    job_system_t sys;
    phys_job_context_t ctx;
    make_job_infra(&sys, &ctx);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stage_narrowphase_par(&args_par, &ctx, &arena);

    phys_frame_arena_destroy(&arena);
    tear_job_infra(&sys, &ctx);

    /* Must not exceed buffer capacity. */
    ASSERT_TRUE(count_par <= MAX_CAND);
    /* Should have filled to capacity since we have 20 overlapping pairs. */
    ASSERT_EQ_UINT(MAX_CAND, count_par);

    /* Verify no out-of-bounds writes: check each candidate has valid body
     * indices (all < N_BODIES) and at least 1 contact. */
    for (uint32_t i = 0; i < count_par; i++) {
        ASSERT_TRUE(cand_par[i].body_a < N_BODIES);
        ASSERT_TRUE(cand_par[i].body_b < N_BODIES);
        ASSERT_TRUE(cand_par[i].contact_count >= 1);
    }

    return 0;
}

/* ── Test table and runner ──────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"par_np_identical_to_seq",  test_par_np_identical_to_seq},
    {"par_np_batch_64",          test_par_np_batch_64},
    {"par_np_zero_pairs",        test_par_np_zero_pairs},
    {"par_np_no_contacts",       test_par_np_no_contacts},
    {"par_np_mixed_shapes",      test_par_np_mixed_shapes},
    {"par_np_max_candidates",    test_par_np_max_candidates},
};

int main(void)
{
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        fflush(stdout);
        int rc = tc->fn();
        if (rc == 0) {
            passed++;
            printf("OK %s\n", tc->name);
        } else {
            fprintf(stderr, "Test failed: %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
