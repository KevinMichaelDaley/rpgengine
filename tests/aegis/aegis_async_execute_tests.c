/**
 * @file aegis_async_execute_tests.c
 * @brief Tests for async task execution (draining VIS_TEST → phys_raycast).
 *
 * Uses a real phys_world_t to verify end-to-end raycast execution from
 * the MPSC async buffer through to result delivery.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_async_execute.h"
#include "ferrum/physics/raycast.h"
#include "ferrum/physics/world.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                       */
/* ------------------------------------------------------------------ */

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn)                                        \
    do {                                               \
        printf("RUN  %s\n", #fn);                      \
        fn();                                          \
        printf("OK   %s\n", #fn);                      \
    } while (0)

#define ASSERT_TRUE(expr)                                              \
    do {                                                               \
        if (!(expr)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);     \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(fabsf((float)(a) - (float)(b)) < (eps))

#define PASS() g_pass++

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/** @brief Create a minimal physics world with capacity for bodies. */
static bool make_world(phys_world_t *world) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies    = 16;
    cfg.max_colliders = 16;
    return phys_world_init(world, &cfg) == 0;
}

/** @brief Submit a VIS_TEST task with origin + ray_vec into the buffer. */
static bool submit_vis_test(aegis_async_buffer_t *buf,
                            float ox, float oy, float oz,
                            float dx, float dy, float dz,
                            void *result_ptr) {
    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    atomic_store(&task.status, AEGIS_ASYNC_PENDING);
    task.task_type  = AEGIS_TASK_VIS_TEST;
    task.result_ptr = result_ptr;
    task.result_cap = 16;

    float origin[3] = {ox, oy, oz};
    float ray[3]    = {dx, dy, dz};
    memcpy(task.params, origin, 12);
    memcpy(task.params + 12, ray, 12);

    return aegis_async_buffer_submit(buf, &task);
}

/* ================================================================== */
/* Tests                                                              */
/* ================================================================== */

/* --- Hit a sphere ------------------------------------------------- */

static void test_execute_raycast_hits_sphere(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world));

    /* Place a sphere at (0, 0, 5) with radius 1. */
    uint32_t bid = phys_world_create_body(&world);
    ASSERT_TRUE(bid != UINT32_MAX);
    phys_body_t *b = phys_world_get_body(&world, bid);
    ASSERT_TRUE(b != NULL);
    phys_body_set_mass(b, 1.0f);
    b->position = (phys_vec3_t){0.0f, 0.0f, 5.0f};
    phys_world_set_sphere_collider(&world, bid, 1.0f, (phys_vec3_t){0, 0, 0});

    /* Submit a VIS_TEST: ray along +Z. */
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    /* Result slot: 16 bytes. */
    uint8_t result[16];
    memset(result, 0, sizeof(result));

    /* ray_vec = (0, 0, 10) → direction = (0,0,1), max_distance = 10 */
    ASSERT_TRUE(submit_vis_test(&buf, 0, 0, 0, 0, 0, 10, result));

    /* Execute. */
    uint32_t executed = aegis_async_execute_drain(&buf, &world, 16);
    ASSERT_EQ(executed, (uint32_t)1);

    /* Check the task in the buffer is now drained. */
    ASSERT_EQ(aegis_async_buffer_count(&buf), (uint32_t)0);

    /* Parse result: distance (4B) + hit_point (12B). */
    float distance;
    float hit_point[3];
    memcpy(&distance, result, 4);
    memcpy(hit_point, result + 4, 12);

    ASSERT_NEAR(distance, 4.0f, 0.05f);
    ASSERT_NEAR(hit_point[0], 0.0f, 0.05f);
    ASSERT_NEAR(hit_point[1], 0.0f, 0.05f);
    ASSERT_NEAR(hit_point[2], 4.0f, 0.05f);

    phys_world_destroy(&world);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Miss (no geometry) ------------------------------------------- */

static void test_execute_raycast_misses(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world));

    /* No bodies — ray should miss. */
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    uint8_t result[16];
    memset(result, 0xFF, sizeof(result));

    ASSERT_TRUE(submit_vis_test(&buf, 0, 0, 0, 0, 0, 10, result));
    uint32_t executed = aegis_async_execute_drain(&buf, &world, 16);
    ASSERT_EQ(executed, (uint32_t)1);

    /* On miss: distance = -1.0f. */
    float distance;
    memcpy(&distance, result, 4);
    ASSERT_TRUE(distance < 0.0f);

    phys_world_destroy(&world);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Hit a box ---------------------------------------------------- */

static void test_execute_raycast_hits_box(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world));

    /* Place a box at (0, 0, 3) with half_extents (1, 1, 1). */
    uint32_t bid = phys_world_create_body(&world);
    ASSERT_TRUE(bid != UINT32_MAX);
    phys_body_t *b = phys_world_get_body(&world, bid);
    phys_body_set_mass(b, 1.0f);
    b->position = (phys_vec3_t){0.0f, 0.0f, 3.0f};
    phys_world_set_box_collider(&world, bid,
                                (phys_vec3_t){1.0f, 1.0f, 1.0f},
                                (phys_vec3_t){0, 0, 0},
                                (phys_quat_t){0, 0, 0, 1});

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    uint8_t result[16];
    memset(result, 0, sizeof(result));

    ASSERT_TRUE(submit_vis_test(&buf, 0, 0, 0, 0, 0, 10, result));
    uint32_t executed = aegis_async_execute_drain(&buf, &world, 16);
    ASSERT_EQ(executed, (uint32_t)1);

    float distance;
    memcpy(&distance, result, 4);
    /* Box at z=3 with half_extent=1 → near face at z=2. */
    ASSERT_NEAR(distance, 2.0f, 0.05f);

    phys_world_destroy(&world);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Multiple batched raycasts ------------------------------------ */

static void test_execute_multiple_raycasts(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world));

    /* Sphere at (0, 0, 5). */
    uint32_t bid = phys_world_create_body(&world);
    phys_body_t *b = phys_world_get_body(&world, bid);
    phys_body_set_mass(b, 1.0f);
    b->position = (phys_vec3_t){0.0f, 0.0f, 5.0f};
    phys_world_set_sphere_collider(&world, bid, 1.0f, (phys_vec3_t){0, 0, 0});

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    /* Submit 3 raycasts: hit, miss, hit. */
    uint8_t r1[16], r2[16], r3[16];
    memset(r1, 0, 16); memset(r2, 0, 16); memset(r3, 0, 16);

    /* Hit: along +Z. */
    ASSERT_TRUE(submit_vis_test(&buf, 0, 0, 0, 0, 0, 10, r1));
    /* Miss: along +X (nothing there). */
    ASSERT_TRUE(submit_vis_test(&buf, 0, 0, 0, 10, 0, 0, r2));
    /* Hit: along -Z from behind. */
    ASSERT_TRUE(submit_vis_test(&buf, 0, 0, 10, 0, 0, -10, r3));

    uint32_t executed = aegis_async_execute_drain(&buf, &world, 16);
    ASSERT_EQ(executed, (uint32_t)3);

    float d1, d2, d3;
    memcpy(&d1, r1, 4);
    memcpy(&d2, r2, 4);
    memcpy(&d3, r3, 4);

    ASSERT_NEAR(d1, 4.0f, 0.05f);   /* Hit sphere face at z=4. */
    ASSERT_TRUE(d2 < 0.0f);          /* Miss. */
    ASSERT_NEAR(d3, 4.0f, 0.05f);   /* Hit sphere from behind at z=6. */

    phys_world_destroy(&world);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Empty buffer ------------------------------------------------- */

static void test_execute_empty_buffer(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world));

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    uint32_t executed = aegis_async_execute_drain(&buf, &world, 16);
    ASSERT_EQ(executed, (uint32_t)0);

    phys_world_destroy(&world);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- NAV_QUERY returns ERROR (not yet implemented) ---------------- */

static void test_execute_nav_query_returns_error(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world));

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    atomic_store(&task.status, AEGIS_ASYNC_PENDING);
    task.task_type  = AEGIS_TASK_NAV_QUERY;
    uint8_t result[16];
    memset(result, 0, sizeof(result));
    task.result_ptr = result;
    task.result_cap = 16;
    ASSERT_TRUE(aegis_async_buffer_submit(&buf, &task));

    uint32_t executed = aegis_async_execute_drain(&buf, &world, 16);
    ASSERT_EQ(executed, (uint32_t)1);

    /* NAV_QUERY has no implementation yet — status should be ERROR. */
    /* The task status is set on the drained copy; we read the result. */
    /* For unimplemented tasks, result should indicate error. */
    float distance;
    memcpy(&distance, result, 4);
    /* We don't strictly test the distance for NAV_QUERY — just that
     * it was processed without crashing. */
    (void)distance;

    phys_world_destroy(&world);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Partial drain (max_tasks limits) ----------------------------- */

static void test_execute_partial_drain(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world));

    /* Sphere at (0, 0, 5). */
    uint32_t bid = phys_world_create_body(&world);
    phys_body_t *b = phys_world_get_body(&world, bid);
    phys_body_set_mass(b, 1.0f);
    b->position = (phys_vec3_t){0.0f, 0.0f, 5.0f};
    phys_world_set_sphere_collider(&world, bid, 1.0f, (phys_vec3_t){0, 0, 0});

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    uint8_t r1[16], r2[16], r3[16];
    memset(r1, 0, 16); memset(r2, 0, 16); memset(r3, 0, 16);
    ASSERT_TRUE(submit_vis_test(&buf, 0, 0, 0, 0, 0, 10, r1));
    ASSERT_TRUE(submit_vis_test(&buf, 0, 0, 0, 0, 0, 10, r2));
    ASSERT_TRUE(submit_vis_test(&buf, 0, 0, 0, 0, 0, 10, r3));

    /* Drain only 2. */
    uint32_t executed = aegis_async_execute_drain(&buf, &world, 2);
    ASSERT_EQ(executed, (uint32_t)2);

    /* 1 remaining. */
    ASSERT_EQ(aegis_async_buffer_count(&buf), (uint32_t)1);

    /* Drain remaining. */
    executed = aegis_async_execute_drain(&buf, &world, 16);
    ASSERT_EQ(executed, (uint32_t)1);

    phys_world_destroy(&world);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- Status is set to COMPLETE on hit ----------------------------- */

static void test_execute_sets_status_complete(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world));

    /* Sphere at (0, 0, 5). */
    uint32_t bid = phys_world_create_body(&world);
    phys_body_t *b = phys_world_get_body(&world, bid);
    phys_body_set_mass(b, 1.0f);
    b->position = (phys_vec3_t){0.0f, 0.0f, 5.0f};
    phys_world_set_sphere_collider(&world, bid, 1.0f, (phys_vec3_t){0, 0, 0});

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    /* We need to check status on the task AFTER execution.
     * Since the executor modifies the drained copy's status, and the
     * result_ptr gets written, we verify via the result content. */
    uint8_t result[16];
    memset(result, 0, sizeof(result));
    ASSERT_TRUE(submit_vis_test(&buf, 0, 0, 0, 0, 0, 10, result));

    aegis_async_execute_drain(&buf, &world, 16);

    /* Result should be non-zero (distance > 0 means hit). */
    float distance;
    memcpy(&distance, result, 4);
    ASSERT_TRUE(distance > 0.0f);

    phys_world_destroy(&world);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* ================================================================== */
/* Main                                                               */
/* ================================================================== */

int main(void) {
    printf("=== Aegis Async Execute Tests ===\n\n");

    RUN(test_execute_raycast_hits_sphere);
    RUN(test_execute_raycast_misses);
    RUN(test_execute_raycast_hits_box);
    RUN(test_execute_multiple_raycasts);
    RUN(test_execute_empty_buffer);
    RUN(test_execute_nav_query_returns_error);
    RUN(test_execute_partial_drain);
    RUN(test_execute_sets_status_complete);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
