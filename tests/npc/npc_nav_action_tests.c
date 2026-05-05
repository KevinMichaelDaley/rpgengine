/**
 * @file npc_nav_action_tests.c
 * @brief GOTO navigation tool: target resolution, validation, async submit.
 */

#include "ferrum/npc/npc_nav_action.h"
#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_async_execute.h"
#include "ferrum/npc/npc_nav_world.h"
#include "ferrum/npc/npc_svo.h"
#include "ferrum/npc/npc_pathfind.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUN(fn) do { printf("  %-48s ", #fn); fn(); } while (0)
#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL (%s:%d)\n", __FILE__, __LINE__); \
        g_fail++; \
        return; \
    } \
} while (0)
#define ASSERT_INT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        printf("FAIL (%s:%d) expected %d got %d\n", \
               __FILE__, __LINE__, (int)(exp), (int)(act)); \
        g_fail++; \
        return; \
    } \
} while (0)
#define ASSERT_FLOAT_NEAR(exp, act, tol) do { \
    if (fabsf((exp) - (act)) > (tol)) { \
        printf("FAIL (%s:%d) expected %.6f got %.6f\n", \
               __FILE__, __LINE__, (float)(exp), (float)(act)); \
        g_fail++; \
        return; \
    } \
} while (0)
#define PASS() do { printf("PASS\n"); g_pass++; } while (0)

static int g_pass = 0;
static int g_fail = 0;

extern struct npc_nav_world *g_aegis_nav_world;

static void make_floor(phys_triangle_t tri[2], float x0, float y0,
                       float x1, float y1, float z) {
    phys_vec3_t a = {x0,y0,z}, b = {x1,y0,z}, c = {x1,y1,z}, d = {x0,y1,z};
    tri[0] = (phys_triangle_t){{a,b,c}};
    tri[1] = (phys_triangle_t){{a,c,d}};
}

/* ── Tests ──────────────────────────────────────────────────────── */

static void test_goto_unknown_target(void) {
    aegis_async_buffer_t buf;
    aegis_async_buffer_init(&buf, 8);

    char result[256];
    memset(result, 0, sizeof(result));
    bool ok = npc_nav_action_goto(NULL, 0, NULL, "nonexistent_target",
                                  (phys_vec3_t){0,0,0}, false, false,
                                  &buf, result, sizeof(result));
    ASSERT_TRUE(!ok);
    ASSERT_TRUE(strstr(result, "not available") != NULL || strstr(result, "unknown") != NULL);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_goto_rooted_fails(void) {
    aegis_async_buffer_t buf;
    aegis_async_buffer_init(&buf, 8);

    char result[256];
    memset(result, 0, sizeof(result));
    /* Target exists at (10,10,5) but actor is rooted. */
    phys_vec3_t target = {10.0f, 10.0f, 5.0f};
    bool ok = npc_nav_action_goto(NULL, 42, NULL, "campfire",
                                  target, true, false,
                                  &buf, result, sizeof(result));
    ASSERT_TRUE(!ok);
    ASSERT_TRUE(strstr(result, "rooted") != NULL || strstr(result, "stunned") != NULL);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_goto_stunned_fails(void) {
    aegis_async_buffer_t buf;
    aegis_async_buffer_init(&buf, 8);

    char result[256];
    memset(result, 0, sizeof(result));
    phys_vec3_t target = {10.0f, 10.0f, 5.0f};
    bool ok = npc_nav_action_goto(NULL, 42, NULL, "campfire",
                                  target, false, true,
                                  &buf, result, sizeof(result));
    ASSERT_TRUE(!ok);
    ASSERT_TRUE(strstr(result, "stunned") != NULL);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_goto_submits_nav_query(void) {
    /* Build nav world. */
    npc_nav_world_t *nw = (npc_nav_world_t *)calloc(1, sizeof(npc_nav_world_t));
    npc_nav_world_init(nw);
    phys_aabb_t wb = {{0, 0, 0}, {16, 16, 16}};
    npc_svo_grid_init(&nw->svo, wb, 4);
    nw->built = true;

    phys_triangle_t floor[2];
    make_floor(floor, 1.0f, 1.0f, 15.0f, 15.0f, 4.0f);
    npc_svo_rasterize_mesh(&nw->svo, floor, 2);

    g_aegis_nav_world = nw;

    aegis_async_buffer_t buf;
    aegis_async_buffer_init(&buf, 8);

    char result[256];
    memset(result, 0, sizeof(result));
    phys_vec3_t target = {14.0f, 14.0f, 5.0f};
    bool ok = npc_nav_action_goto(nw, 42, NULL, "campfire",
                                  target, false, false,
                                  &buf, result, sizeof(result));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(strstr(result, "Moving to") != NULL);

    /* Verify a task was submitted. */
    aegis_async_task_t drained[1];
    uint32_t dc = aegis_async_buffer_drain(&buf, drained, 1);
    ASSERT_INT_EQ(1, dc);
    ASSERT_INT_EQ(AEGIS_TASK_NAV_QUERY, drained[0].task_type);

    g_aegis_nav_world = NULL;
    npc_nav_world_destroy(nw);
    free(nw);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_register_landmark_resolve(void) {
    phys_vec3_t forge = {100.0f, 200.0f, 50.0f};
    bool ok = npc_nav_action_register_landmark("iron_forge", forge);
    ASSERT_TRUE(ok);

    phys_vec3_t pos;
    bool resolved = npc_nav_action_resolve_target(NULL, 0, "iron_forge",
                                                  &pos);
    ASSERT_TRUE(resolved);
    ASSERT_FLOAT_NEAR(100.0f, pos.x, 0.001f);
    ASSERT_FLOAT_NEAR(200.0f, pos.y, 0.001f);
    ASSERT_FLOAT_NEAR(50.0f, pos.z, 0.001f);
    PASS();
}

static void test_resolve_unknown_landmark(void) {
    phys_vec3_t pos;
    bool resolved = npc_nav_action_resolve_target(NULL, 0, "nineveh",
                                                  &pos);
    ASSERT_TRUE(!resolved);
    PASS();
}

static void test_goto_result_struct(void) {
    /* Verify the result struct layout. */
    char buf[256];
    memset(buf, 0, sizeof(buf));
    npc_nav_action_result_t *r = (npc_nav_action_result_t *)buf;
    r->status = 0;
    strcpy(r->message, "Moving to campfire.");
    ASSERT_INT_EQ(0, r->status);
    ASSERT_TRUE(strcmp(r->message, "Moving to campfire.") == 0);
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    printf("npc_nav_action_tests\n");
    RUN(test_goto_unknown_target);
    RUN(test_goto_rooted_fails);
    RUN(test_goto_stunned_fails);
    RUN(test_goto_submits_nav_query);
    RUN(test_register_landmark_resolve);
    RUN(test_resolve_unknown_landmark);
    RUN(test_goto_result_struct);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
