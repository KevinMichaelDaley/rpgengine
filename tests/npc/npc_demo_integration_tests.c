/**
 * @file npc_demo_integration_tests.c
 * @brief NPC demo integration tests: spawn, tick, state, nav world.
 */

#include "ferrum/npc/npc_demo.h"
#include "ferrum/npc/npc_nav_world.h"
#include "ferrum/npc/npc_svo.h"
#include "ferrum/npc/npc_knowledge_graph.h"
#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_async_execute.h"
#include "ferrum/aegis/aegis_runtime.h"
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/raycast.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/net/topic_channel.h"

#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
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
    if ((int)(exp) != (int)(act)) { \
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

/* ── Helper: create a simple floor mesh ──────────────────────────── */

static void make_floor_tris(phys_triangle_t tri[2], float x0, float y0,
                            float x1, float y1, float z) {
    phys_vec3_t a = {x0, y0, z}, b = {x1, y0, z};
    phys_vec3_t c = {x1, y1, z}, d = {x0, y1, z};
    tri[0] = (phys_triangle_t){{a, b, c}};
    tri[1] = (phys_triangle_t){{a, c, d}};
}

/* ── helper: create a minimal topic channel ──────────────────────── */

static fr_topic_channel_t *make_cmd_channel(void) {
    fr_topic_channel_config_t cfg = {
        .capacity       = 64u,
        .capacity_bytes = 64u * 1024u,
        .max_message_size = 1024u,
        .backpressure   = FR_TOPIC_BACKPRESSURE_DROP_OLDEST,
    };
    return fr_topic_channel_create(&cfg);
}

/* ── Test 1: NPC spawn creates entity with correct fields ────────── */

static void test_npc_spawn_fields(void) {
    fr_topic_channel_t *ch = make_cmd_channel();
    ASSERT_TRUE(ch != NULL);

    npc_demo_spawn_t spawn;
    memset(&spawn, 0, sizeof(spawn));
    spawn.entity_id = 100u;
    spawn.position[0] = 1.0f;
    spawn.position[1] = 2.0f;
    spawn.position[2] = 3.0f;
    memcpy(spawn.name, "TestOrc", 8);
    spawn.statblock[0] = 100;
    spawn.statblock[1] = 50;
    spawn.statblock[2] = 5;
    spawn.statblock[3] = 2;

    uint32_t eid = npc_demo_spawn_npc(ch, &spawn);
    ASSERT_INT_EQ(100u, (int)eid);

    uint32_t count = 0;
    const npc_demo_npc_t *npcs = npc_demo_npc_list(&count);
    ASSERT_TRUE(count >= 1);
    ASSERT_TRUE(npcs[0].active);
    ASSERT_INT_EQ(100u, (int)npcs[0].config.entity_id);
    ASSERT_INT_EQ(100, (int)npcs[0].config.statblock[0]);
    ASSERT_INT_EQ(50, (int)npcs[0].config.statblock[1]);
    ASSERT_TRUE(npcs[0].config.position[0] == 1.0f);
    ASSERT_TRUE(npcs[0].config.position[1] == 2.0f);
    ASSERT_TRUE(npcs[0].config.position[2] == 3.0f);
    ASSERT_TRUE(strncmp(npcs[0].config.name, "TestOrc", 8) == 0);

    fr_topic_channel_destroy(ch);
    PASS();
}

/* ── Test 2: NPC tick processes one turn without crashing ────────── */

static void test_npc_tick_no_crash(void) {
    fr_topic_channel_t *ch = make_cmd_channel();
    ASSERT_TRUE(ch != NULL);

    npc_demo_spawn_t spawn;
    memset(&spawn, 0, sizeof(spawn));
    spawn.entity_id = 200u;

    npc_demo_spawn_npc(ch, &spawn);

    /* Build minimal phys world for async drain. */
    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = 4; wcfg.max_colliders = 4;
    phys_world_t pw;
    phys_world_init(&pw, &wcfg);

    aegis_async_buffer_t buf;
    aegis_async_buffer_init(&buf, 16);

    /* Create minimal script runtime. */
    aegis_script_runtime_t rt;
    aegis_runtime_config_t rcfg;
    memset(&rcfg, 0, sizeof(rcfg));
    rcfg.max_instances     = 4;
    rcfg.max_subscriptions = 16;
    rcfg.event_queue_cap   = 16;
    rcfg.vm_config         = aegis_config_default();
    rcfg.vm_config.arena_size  = 512;
    rcfg.vm_config.static_max  = 32;
    rcfg.vm_config.stack_max   = 32;
    bool rt_ok = aegis_script_runtime_init(&rt, &rcfg);
    ASSERT_TRUE(rt_ok);

    /* Call tick — should not crash. */
    npc_demo_tick(&rt, &buf, &pw);

    uint32_t count = 0;
    const npc_demo_npc_t *npcs = npc_demo_npc_list(&count);
    ASSERT_TRUE(count >= 1);
    ASSERT_TRUE(npcs[0].tick_count >= 1);

    aegis_script_runtime_destroy(&rt);
    aegis_async_buffer_destroy(&buf);
    phys_world_destroy(&pw);
    fr_topic_channel_destroy(ch);
    PASS();
}

/* ── Test 3: NPC state persists across ticks ─────────────────────── */

static void test_npc_state_persists(void) {
    fr_topic_channel_t *ch = make_cmd_channel();
    ASSERT_TRUE(ch != NULL);

    npc_demo_spawn_t spawn;
    memset(&spawn, 0, sizeof(spawn));
    spawn.entity_id = 300u;

    npc_demo_spawn_npc(ch, &spawn);

    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = 4; wcfg.max_colliders = 4;
    phys_world_t pw;
    phys_world_init(&pw, &wcfg);

    aegis_async_buffer_t buf;
    aegis_async_buffer_init(&buf, 16);

    aegis_script_runtime_t rt;
    aegis_runtime_config_t rcfg;
    memset(&rcfg, 0, sizeof(rcfg));
    rcfg.max_instances     = 4;
    rcfg.max_subscriptions = 16;
    rcfg.event_queue_cap   = 16;
    rcfg.vm_config         = aegis_config_default();
    rcfg.vm_config.arena_size  = 512;
    rcfg.vm_config.static_max  = 32;
    rcfg.vm_config.stack_max   = 32;
    aegis_script_runtime_init(&rt, &rcfg);

    npc_demo_tick(&rt, &buf, &pw);

    uint32_t count = 0;
    const npc_demo_npc_t *npcs = npc_demo_npc_list(&count);
    ASSERT_TRUE(npcs[0].tick_count >= 1);
    uint32_t ticks_before = npcs[0].tick_count;

    npc_demo_tick(&rt, &buf, &pw);

    npcs = npc_demo_npc_list(&count);
    ASSERT_TRUE(npcs[0].tick_count == ticks_before + 1);
    ASSERT_TRUE(npcs[0].active);

    aegis_script_runtime_destroy(&rt);
    aegis_async_buffer_destroy(&buf);
    phys_world_destroy(&pw);
    fr_topic_channel_destroy(ch);
    PASS();
}

/* ── Test 4: Nav world builds from simple geometry ───────────────── */

static void test_nav_world_geometry(void) {
    npc_nav_world_t nw;
    bool ok = npc_nav_world_init(&nw);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(!nw.built);

    phys_aabb_t world_aabb = {{0, 0, 0}, {16, 16, 16}};
    npc_svo_grid_init(&nw.svo, world_aabb, 4);
    nw.built = true;

    phys_triangle_t floor[2];
    make_floor_tris(floor, 1.0f, 1.0f, 15.0f, 15.0f, 4.0f);
    npc_svo_rasterize_mesh(&nw.svo, floor, 2);

    /* Execute a simple path query. */
    uint8_t params[64];
    memset(params, 0, sizeof(params));
    float start[3] = {2.0f, 2.0f, 5.0f};
    float goal[3]  = {14.0f, 14.0f, 5.0f};
    memcpy(params, start, 12);
    memcpy(params + 12, goal, 12);
    uint32_t strat = 0; /* NPC_PATH_SVO_ONLY */
    memcpy(params + 24, &strat, 4);
    float rad = 0.3f, h = 1.8f;
    memcpy(params + 28, &rad, 4);
    memcpy(params + 32, &h, 4);
    uint32_t max_wp = 64;
    memcpy(params + 36, &max_wp, 4);

    uint8_t result[4096];
    memset(result, 0, sizeof(result));

    npc_nav_world_execute(&nw, params, result, sizeof(result));

    int32_t status;
    memcpy(&status, result, 4);
    ASSERT_INT_EQ(0, (int)status);

    uint32_t wp_count;
    memcpy(&wp_count, result + 4, 4);
    ASSERT_TRUE(wp_count >= 2);

    npc_nav_world_destroy(&nw);
    PASS();
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(void) {
    printf("npc_demo_integration_tests\n");
    RUN(test_npc_spawn_fields);
    RUN(test_npc_tick_no_crash);
    RUN(test_npc_state_persists);
    RUN(test_nav_world_geometry);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
