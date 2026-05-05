/**
 * @file npc_nav_integration_tests.c
 * @brief Navigation integration: nav world + async executor drain.
 */

#include "ferrum/npc/npc_svo.h"
#include "ferrum/npc/npc_nav_world.h"
#include "ferrum/npc/npc_pathfind.h"
#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_async_execute.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/raycast.h"

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
#define PASS() do { printf("PASS\n"); g_pass++; } while (0)

static int g_pass = 0;
static int g_fail = 0;

extern struct npc_nav_world *g_aegis_nav_world;

static void make_floor(phys_triangle_t tri[2], float x0, float y0,
                       float x1, float y1, float z) {
    phys_vec3_t a = {x0, y0, z}, b = {x1, y0, z};
    phys_vec3_t c = {x1, y1, z}, d = {x0, y1, z};
    tri[0] = (phys_triangle_t){{a, b, c}};
    tri[1] = (phys_triangle_t){{a, c, d}};
}

static void test_nav_world_init_destroy(void) {
    npc_nav_world_t nw;
    bool ok = npc_nav_world_init(&nw);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(!nw.built);
    npc_nav_world_destroy(&nw);
    PASS();
}

static void test_nav_world_execute_straight_path(void) {
    npc_nav_world_t nw;
    npc_nav_world_init(&nw);
    phys_aabb_t world = {{0, 0, 0}, {16, 16, 16}};
    npc_svo_grid_init(&nw.svo, world, 4);
    nw.built = true;

    phys_triangle_t floor[2];
    make_floor(floor, 1.0f, 1.0f, 15.0f, 15.0f, 4.0f);
    npc_svo_rasterize_mesh(&nw.svo, floor, 2);

    uint8_t params[64];
    memset(params, 0, sizeof(params));
    float start[3] = {2.0f, 2.0f, 5.0f};
    float goal[3]  = {14.0f, 14.0f, 5.0f};
    memcpy(params, start, 12);
    memcpy(params + 12, goal, 12);
    uint32_t strat = NPC_PATH_SVO_ONLY;
    memcpy(params + 24, &strat, 4);
    float rad = 0.3f, h = 1.8f;
    memcpy(params + 28, &rad, 4);
    memcpy(params + 32, &h, 4);
    uint32_t max_wp = 64;
    memcpy(params + 36, &max_wp, 4);

    uint8_t result[4096];
    memset(result, 0, sizeof(result));

    npc_nav_world_execute(&nw, params, result, sizeof(result));

    int32_t status; uint32_t wp_count;
    memcpy(&status, result, 4);
    memcpy(&wp_count, result + 4, 4);
    ASSERT_INT_EQ(0, status);
    ASSERT_TRUE(wp_count >= 2);

    npc_nav_world_destroy(&nw);
    PASS();
}

static void test_nav_async_integration(void) {
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

    /* Build a minimal phys_world for the drain function. */
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 4; cfg.max_colliders = 4;
    phys_world_t pw;
    phys_world_init(&pw, &cfg);

    /* Submit nav query to async buffer. */
    aegis_async_buffer_t buf;
    aegis_async_buffer_init(&buf, 16);

    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    atomic_store(&task.status, AEGIS_ASYNC_PENDING);
    task.task_type = AEGIS_TASK_NAV_QUERY;

    uint8_t result_buf[4096];
    memset(result_buf, 0, sizeof(result_buf));
    task.result_ptr = result_buf;
    task.result_cap = sizeof(result_buf);

    float start[3] = {2.0f, 2.0f, 5.0f};
    float goal[3]  = {14.0f, 14.0f, 5.0f};
    memcpy(task.params, start, 12);
    memcpy(task.params + 12, goal, 12);
    uint32_t strat = NPC_PATH_SVO_ONLY;
    memcpy(task.params + 24, &strat, 4);
    float rad = 0.3f, h = 1.8f;
    memcpy(task.params + 28, &rad, 4);
    memcpy(task.params + 32, &h, 4);
    uint32_t max_wp = 64;
    memcpy(task.params + 36, &max_wp, 4);

    ASSERT_TRUE(aegis_async_buffer_submit(&buf, &task));
    uint32_t executed = aegis_async_execute_drain(&buf, &pw, 1);
    ASSERT_INT_EQ(1, executed);

    int32_t status; uint32_t wp_count;
    memcpy(&status, result_buf, 4);
    memcpy(&wp_count, result_buf + 4, 4);
    ASSERT_INT_EQ(0, status);
    ASSERT_TRUE(wp_count >= 2);

    g_aegis_nav_world = NULL;
    npc_nav_world_destroy(nw);
    free(nw);
    phys_world_destroy(&pw);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

int main(void) {
    printf("npc_nav_integration_tests\n");
    RUN(test_nav_world_init_destroy);
    RUN(test_nav_world_execute_straight_path);
    RUN(test_nav_async_integration);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
