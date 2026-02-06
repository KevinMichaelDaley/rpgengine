/**
 * @file p037_physics_aabb_update_tests.c
 * @brief Unit tests for Stage 4: AABB Update (phys-105).
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "ferrum/physics/aabb_update.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/tier_list.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                \
                    __FILE__, __LINE__, #cond);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                       \
    do {                                                                       \
        if (fabsf((float)(exp) - (float)(act)) > (eps)) {                      \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f\n", __FILE__, __LINE__,            \
                    (double)(exp), (double)(act));                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_VEC3_NEAR(exp, act, eps)                                        \
    do {                                                                       \
        ASSERT_FLOAT_NEAR((exp).x, (act).x, (eps));                            \
        ASSERT_FLOAT_NEAR((exp).y, (act).y, (eps));                            \
        ASSERT_FLOAT_NEAR((exp).z, (act).z, (eps));                            \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static const phys_vec3_t ZERO_VEC = {0, 0, 0};
static const phys_quat_t IDENTITY_QUAT = {0, 0, 0, 1};

/**
 * Helper: manually set up a tier list with a stack-allocated indices array.
 * Avoids needing a frame arena for simple tests.
 */
static void setup_tier_list(phys_tier_list_t *list, uint32_t *buf,
                            uint32_t capacity) {
    list->indices = buf;
    list->count = 0;
    list->capacity = capacity;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * test_aabb_update_active_bodies:
 * 2 bodies in T0 with sphere colliders. Verify AABBs computed correctly.
 */
static int test_aabb_update_active_bodies(void) {
    phys_body_t bodies[2];
    phys_collider_t colliders[2];
    phys_sphere_t spheres[2] = {{.radius = 1.0f}, {.radius = 0.5f}};
    phys_aabb_t aabbs[2];

    for (int i = 0; i < 2; ++i) {
        phys_body_init(&bodies[i]);
        bodies[i].position = (phys_vec3_t){(float)(i * 10), 0, 0};
        phys_collider_init_sphere(&colliders[i], (uint32_t)i, ZERO_VEC);
    }

    /* Set up tier lists manually. Both bodies in T0. */
    phys_tier_lists_t tier_lists;
    uint32_t t0_buf[4];
    uint32_t empty_buf[4];
    memset(&tier_lists, 0, sizeof(tier_lists));
    setup_tier_list(&tier_lists.tiers[PHYS_TIER_0_DIRECT], t0_buf, 4);
    for (int t = 1; t < PHYS_TIER_COUNT; ++t) {
        setup_tier_list(&tier_lists.tiers[t], empty_buf, 4);
    }
    phys_tier_list_add(&tier_lists.tiers[PHYS_TIER_0_DIRECT], 0);
    phys_tier_list_add(&tier_lists.tiers[PHYS_TIER_0_DIRECT], 1);

    phys_aabb_update_args_t args = {
        .bodies     = bodies,
        .colliders  = colliders,
        .spheres    = spheres,
        .boxes      = NULL,
        .capsules   = NULL,
        .aabbs_out  = aabbs,
        .tier_lists = &tier_lists
    };

    phys_stage_aabb_update(&args);

    /* Body 0: sphere r=1 at (0,0,0) → AABB [-1,-1,-1] to [1,1,1] */
    ASSERT_VEC3_NEAR(((phys_vec3_t){-1, -1, -1}), aabbs[0].min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 1,  1,  1}), aabbs[0].max, 0.001f);

    /* Body 1: sphere r=0.5 at (10,0,0) → AABB [9.5,-0.5,-0.5] to [10.5,0.5,0.5] */
    ASSERT_VEC3_NEAR(((phys_vec3_t){9.5f, -0.5f, -0.5f}), aabbs[1].min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){10.5f, 0.5f,  0.5f}), aabbs[1].max, 0.001f);

    return 0;
}

/**
 * test_aabb_update_skips_sleeping:
 * 1 T0 body + 1 T5 body. T5 body's AABB should remain at sentinel.
 */
static int test_aabb_update_skips_sleeping(void) {
    phys_body_t bodies[2];
    phys_collider_t colliders[2];
    phys_sphere_t spheres[2] = {{.radius = 1.0f}, {.radius = 1.0f}};
    phys_aabb_t aabbs[2];

    for (int i = 0; i < 2; ++i) {
        phys_body_init(&bodies[i]);
        bodies[i].position = (phys_vec3_t){(float)(i * 10), 0, 0};
        phys_collider_init_sphere(&colliders[i], (uint32_t)i, ZERO_VEC);
    }

    /* Set sentinel AABB for the sleeping body (index 1). */
    aabbs[1].min = (phys_vec3_t){999, 999, 999};
    aabbs[1].max = (phys_vec3_t){999, 999, 999};

    /* Body 0 in T0, body 1 in T5 (sleeping). */
    phys_tier_lists_t tier_lists;
    uint32_t t0_buf[4], t5_buf[4];
    uint32_t empty_buf[4];
    memset(&tier_lists, 0, sizeof(tier_lists));
    setup_tier_list(&tier_lists.tiers[PHYS_TIER_0_DIRECT], t0_buf, 4);
    setup_tier_list(&tier_lists.tiers[PHYS_TIER_5_SLEEPING], t5_buf, 4);
    for (int t = 1; t < PHYS_TIER_5_SLEEPING; ++t) {
        setup_tier_list(&tier_lists.tiers[t], empty_buf, 4);
    }
    phys_tier_list_add(&tier_lists.tiers[PHYS_TIER_0_DIRECT], 0);
    phys_tier_list_add(&tier_lists.tiers[PHYS_TIER_5_SLEEPING], 1);

    phys_aabb_update_args_t args = {
        .bodies     = bodies,
        .colliders  = colliders,
        .spheres    = spheres,
        .boxes      = NULL,
        .capsules   = NULL,
        .aabbs_out  = aabbs,
        .tier_lists = &tier_lists
    };

    phys_stage_aabb_update(&args);

    /* Body 0 should be processed normally. */
    ASSERT_VEC3_NEAR(((phys_vec3_t){-1, -1, -1}), aabbs[0].min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 1,  1,  1}), aabbs[0].max, 0.001f);

    /* Body 1 (sleeping) AABB should remain at sentinel values. */
    ASSERT_FLOAT_NEAR(999.0f, aabbs[1].min.x, 0.001f);
    ASSERT_FLOAT_NEAR(999.0f, aabbs[1].min.y, 0.001f);
    ASSERT_FLOAT_NEAR(999.0f, aabbs[1].min.z, 0.001f);
    ASSERT_FLOAT_NEAR(999.0f, aabbs[1].max.x, 0.001f);

    return 0;
}

/**
 * test_aabb_update_multiple_tiers:
 * Bodies in T0, T1, T2, T3, T4 all get updated.
 */
static int test_aabb_update_multiple_tiers(void) {
    enum { BODY_COUNT = 5 };
    phys_body_t bodies[BODY_COUNT];
    phys_collider_t colliders[BODY_COUNT];
    phys_sphere_t spheres[BODY_COUNT];
    phys_aabb_t aabbs[BODY_COUNT];

    for (int i = 0; i < BODY_COUNT; ++i) {
        phys_body_init(&bodies[i]);
        bodies[i].position = (phys_vec3_t){(float)(i * 10), 0, 0};
        phys_collider_init_sphere(&colliders[i], (uint32_t)i, ZERO_VEC);
        spheres[i].radius = 1.0f;
        /* Sentinel to verify update. */
        aabbs[i].min = (phys_vec3_t){999, 999, 999};
        aabbs[i].max = (phys_vec3_t){999, 999, 999};
    }

    /* One body per active tier: T0..T4. */
    phys_tier_lists_t tier_lists;
    uint32_t tier_bufs[PHYS_TIER_COUNT][4];
    memset(&tier_lists, 0, sizeof(tier_lists));
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        setup_tier_list(&tier_lists.tiers[t], tier_bufs[t], 4);
    }
    for (int i = 0; i < BODY_COUNT; ++i) {
        phys_tier_list_add(&tier_lists.tiers[i], (uint32_t)i);
    }

    phys_aabb_update_args_t args = {
        .bodies     = bodies,
        .colliders  = colliders,
        .spheres    = spheres,
        .boxes      = NULL,
        .capsules   = NULL,
        .aabbs_out  = aabbs,
        .tier_lists = &tier_lists
    };

    phys_stage_aabb_update(&args);

    /* All 5 bodies should have valid AABBs (not sentinel). */
    for (int i = 0; i < BODY_COUNT; ++i) {
        float expected_x = (float)(i * 10);
        ASSERT_VEC3_NEAR(((phys_vec3_t){expected_x - 1, -1, -1}),
                         aabbs[i].min, 0.001f);
        ASSERT_VEC3_NEAR(((phys_vec3_t){expected_x + 1,  1,  1}),
                         aabbs[i].max, 0.001f);
    }

    return 0;
}

/**
 * test_aabb_update_box:
 * Box collider AABB computed correctly.
 */
static int test_aabb_update_box(void) {
    phys_body_t body;
    phys_body_init(&body);
    body.position = (phys_vec3_t){5, 5, 5};

    phys_collider_t collider;
    phys_collider_init_box(&collider, 0, ZERO_VEC, IDENTITY_QUAT);

    phys_box_t box = {.half_extents = {2, 3, 4}};
    phys_aabb_t aabb;

    /* Put body in T0. */
    phys_tier_lists_t tier_lists;
    uint32_t tier_bufs[PHYS_TIER_COUNT][4];
    memset(&tier_lists, 0, sizeof(tier_lists));
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        setup_tier_list(&tier_lists.tiers[t], tier_bufs[t], 4);
    }
    phys_tier_list_add(&tier_lists.tiers[PHYS_TIER_0_DIRECT], 0);

    phys_aabb_update_args_t args = {
        .bodies     = &body,
        .colliders  = &collider,
        .spheres    = NULL,
        .boxes      = &box,
        .capsules   = NULL,
        .aabbs_out  = &aabb,
        .tier_lists = &tier_lists
    };

    phys_stage_aabb_update(&args);

    /* Box at (5,5,5) with half_extents (2,3,4), identity rotation
     * → AABB [3,2,1] to [7,8,9] */
    ASSERT_VEC3_NEAR(((phys_vec3_t){3, 2, 1}), aabb.min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){7, 8, 9}), aabb.max, 0.001f);

    return 0;
}

/**
 * test_aabb_update_capsule:
 * Capsule collider AABB computed correctly.
 */
static int test_aabb_update_capsule(void) {
    phys_body_t body;
    phys_body_init(&body);
    body.position = (phys_vec3_t){0, 0, 0};

    phys_collider_t collider;
    phys_collider_init_capsule(&collider, 0, ZERO_VEC, IDENTITY_QUAT);

    phys_capsule_t capsule = {.radius = 1.0f, .half_height = 2.0f};
    phys_aabb_t aabb;

    /* Put body in T0. */
    phys_tier_lists_t tier_lists;
    uint32_t tier_bufs[PHYS_TIER_COUNT][4];
    memset(&tier_lists, 0, sizeof(tier_lists));
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        setup_tier_list(&tier_lists.tiers[t], tier_bufs[t], 4);
    }
    phys_tier_list_add(&tier_lists.tiers[PHYS_TIER_0_DIRECT], 0);

    phys_aabb_update_args_t args = {
        .bodies     = &body,
        .colliders  = &collider,
        .spheres    = NULL,
        .boxes      = NULL,
        .capsules   = &capsule,
        .aabbs_out  = &aabb,
        .tier_lists = &tier_lists
    };

    phys_stage_aabb_update(&args);

    /* Capsule along +Y: endpoints at (0, ±2, 0), each with radius 1.
     * AABB = [-1, -3, -1] to [1, 3, 1]. */
    ASSERT_VEC3_NEAR(((phys_vec3_t){-1, -3, -1}), aabb.min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 1,  3,  1}), aabb.max, 0.001f);

    return 0;
}

/**
 * test_aabb_update_null_safe:
 * Calling with NULL args should not crash.
 */
static int test_aabb_update_null_safe(void) {
    phys_stage_aabb_update(NULL);
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"aabb_update_active_bodies",  test_aabb_update_active_bodies},
    {"aabb_update_skips_sleeping", test_aabb_update_skips_sleeping},
    {"aabb_update_multiple_tiers", test_aabb_update_multiple_tiers},
    {"aabb_update_box",            test_aabb_update_box},
    {"aabb_update_capsule",        test_aabb_update_capsule},
    {"aabb_update_null_safe",      test_aabb_update_null_safe},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
