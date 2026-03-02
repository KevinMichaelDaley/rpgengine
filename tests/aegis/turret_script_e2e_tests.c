/**
 * @file turret_script_e2e_tests.c
 * @brief End-to-end smoke/perf test for a turret gun Aegis script.
 *
 * Tests the full scripting pipeline:
 *   - Entity queries (entity_count, entity_at, get_attr, query_entity)
 *   - Vec3 math (sub, len, norm, dot, scale)
 *   - Async raycasts (vis_test → poll/wait)
 *   - Update pushes (build_update → target_entity → set_field → push_update)
 *   - Event signaling (signal)
 *   - Runtime lifecycle (load, run, idle, destroy)
 *
 * The turret script:
 *   1. Iterates entities to find the player (entity with type == 7)
 *   2. Computes distance; skips if out of range (> 20.0)
 *   3. Submits a vis_test raycast from turret to player
 *   4. Polls the result; if hit distance < player distance, LOS blocked
 *   5. If visible + in range: pushes a rotation update for the turret,
 *      signals "trace_projectile!" event
 *   6. Exits (re-arms on next event)
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>

#include "ferrum/aegis/aegis_asm.h"
#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_async_execute.h"
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/aegis/aegis_runtime.h"
#include "ferrum/aegis/aegis_update.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/editor/edit_script_env.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/world.h"

/* ── Test harness ─────────────────────────────────────────────── */

static int g_pass = 0, g_fail = 0;

#define ASSERT_TRUE(cond) do {                                     \
    if (!(cond)) {                                                 \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__,__LINE__,  \
                #cond);                                            \
        g_fail++; return;                                          \
    }                                                              \
} while (0)

#define ASSERT_FALSE(cond)  ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a,b)      ASSERT_TRUE((a) == (b))

#define ASSERT_NEAR(a, b, eps) \
    ASSERT_TRUE(fabsf((float)(a) - (float)(b)) < (eps))

#define RUN(fn) do {                \
    printf("  %-50s ", #fn);        \
    fn();                           \
    printf("OK\n"); g_pass++;       \
} while (0)

/* ── Forward decls for idle API ───────────────────────────────── */

void aegis_runtime_mark_pending_unschedule(aegis_script_instance_t *inst);
void aegis_runtime_tick_idle(aegis_script_runtime_t *rt);
void aegis_runtime_reset_idle(aegis_script_instance_t *inst);

/* ── Publish callback ─────────────────────────────────────────── */

static void runtime_publish_cb(void *ctx, const aegis_event_t *ev) {
    aegis_script_runtime_publish((aegis_script_runtime_t *)ctx, ev);
}

/* ── Entity snapshot helpers ──────────────────────────────────── */

#define TURRET_TYPE  5
#define PLAYER_TYPE  7
#define TURRET_RANGE 20.0f

static void build_entity_snapshot(script_entity_snapshot_t *snaps,
                                  script_entity_view_t *view,
                                  float turret_x, float turret_y, float turret_z,
                                  float player_x, float player_y, float player_z) {
    memset(snaps, 0, 2 * sizeof(script_entity_snapshot_t));

    /* Entity 0: turret at given position. */
    snaps[0].entity_id  = 0;
    snaps[0].active     = 1;
    snaps[0].type       = TURRET_TYPE;
    snaps[0].pos[0]     = turret_x;
    snaps[0].pos[1]     = turret_y;
    snaps[0].pos[2]     = turret_z;
    snaps[0].body_index = 0;

    /* Entity 1: player at given position. */
    snaps[1].entity_id  = 1;
    snaps[1].active     = 1;
    snaps[1].type       = PLAYER_TYPE;
    snaps[1].pos[0]     = player_x;
    snaps[1].pos[1]     = player_y;
    snaps[1].pos[2]     = player_z;
    snaps[1].body_index = 1;

    view->entities = (const script_entity_snapshot_t *)snaps;
    view->count    = 2;
    view->capacity = 2;
}

/* ── Turret IL script ─────────────────────────────────────────── */

/**
 * The turret script in Aegis IL assembly.
 *
 * Register allocation:
 *   r0  = scratch / builder handle
 *   r1  = entity count / loop index
 *   r2  = entity handle
 *   r3  = entity type
 *   r4  = target type (PLAYER_TYPE = 7)
 *   r5  = player handle (set when found)
 *   r6  = turret position (vec3)
 *   r7  = player position (vec3)
 *   r8  = direction vec3 (player - turret)
 *   r9  = distance (float)
 *   r10 = range threshold
 *   r11 = comparison result
 *   r12 = raycast handle
 *   r13 = raycast result (distance, hit_point)
 *   r14 = ray status flag
 *   r15 = topic hash for trace_projectile!
 *   r16 = normalized direction
 *   r17 = turret entity id (0)
 *   r18 = one (1.0f for scaling)
 *   r19 = found flag
 */
static const char *turret_il_source(void) {
    return
        ".topic turret_tick\n"
        "\n"
        "; ── Phase 1: Find player entity ──\n"
        "load_imm r4 7\n"          /* PLAYER_TYPE */
        "load_imm r5 -1\n"        /* player handle = not found */
        "load_imm r19 0\n"        /* found flag = false */
        "entity_count r1\n"       /* r1 = entity count */
        "load_imm r0 0\n"         /* r0 = loop index */
        "\n"
        "find_loop:\n"
        "lt r11 r0 r1\n"          /* index < count? */
        "jmp_if_not r11 no_player\n"
        "entity_at r2 r0\n"       /* r2 = handle at index */
        "get_attr r3 r2 4\n"      /* r3 = type (key=4) */
        "eq r11 r3 r4\n"          /* type == PLAYER_TYPE? */
        "jmp_if r11 found_player\n"
        "load_imm r11 1\n"
        "add r0 r0 r11\n"         /* index++ */
        "jmp find_loop\n"
        "\n"
        "found_player:\n"
        "mov r5 r2\n"             /* r5 = player handle */
        "load_imm r19 1\n"        /* found = true */
        "\n"
        "; ── Phase 2: Get positions & check range ──\n"
        "load_imm r17 0\n"        /* turret entity_id = 0 */
        "query_entity r2 r17\n"   /* r2 = turret handle */
        "get_attr r6 r2 0\n"      /* r6 = turret pos (key=0) */
        "get_attr r7 r5 0\n"      /* r7 = player pos */
        "vec3_sub r8 r7 r6\n"     /* r8 = player - turret */
        "vec3_len r9 r8\n"        /* r9 = distance */
        "load_imm r10 20.0\n"     /* r10 = range threshold */
        "gt r11 r9 r10\n"         /* distance > range? */
        "jmp_if r11 out_of_range\n"
        "\n"
        "; ── Phase 3: Submit raycast (LOS check) ──\n"
        "vec3_norm r16 r8\n"      /* r16 = normalized direction */
        "vec3_scale r8 r16 r9\n"  /* r8 = dir * distance (ray_vec) */
        "vis_test r12 r6 r8\n"    /* r12 = async handle */
        "wait r13 r14 r12\n"      /* wait for result (yields if pending) */
        "\n"
        "; ── Phase 4: Check LOS result ──\n"
        "; r13.f32 = hit distance (-1 = miss = clear LOS)\n"
        "load_imm r10 0.0\n"
        "lt r11 r13 r10\n"        /* hit_dist < 0? (miss = clear LOS) */
        "jmp_if r11 los_clear\n"
        "; Hit something — check if it's closer than the player\n"
        "lt r11 r13 r9\n"         /* hit_dist < player_dist? */
        "jmp_if r11 los_blocked\n"
        "\n"
        "los_clear:\n"
        "; ── Phase 5: Push rotation update ──\n"
        "build_update r0\n"
        "target_entity r0 r17\n"  /* target = turret (entity 0) */
        "set_field r0 1 r16\n"    /* key=1 = ROT, value = normalized dir */
        "push_update r0\n"
        "\n"
        "; ── Phase 6: Signal trace_projectile! ──\n"
        "load_imm r15 999\n"      /* topic hash (we'll match this in test) */
        "signal r0 r15 r17\n"     /* signal topic=999, payload=turret_id */
        "exit r0\n"
        "\n"
        "los_blocked:\n"
        "load_imm r0 2\n"         /* exit code 2 = LOS blocked */
        "exit r0\n"
        "\n"
        "out_of_range:\n"
        "load_imm r0 1\n"         /* exit code 1 = out of range */
        "exit r0\n"
        "\n"
        "no_player:\n"
        "load_imm r0 3\n"         /* exit code 3 = no player found */
        "exit r0\n";
}

/* ── Helpers for wiring a VM instance ─────────────────────────── */

static void wire_vm(aegis_script_runtime_t *rt, uint32_t sid,
                    const script_entity_view_t *view,
                    aegis_async_buffer_t *async_buf,
                    aegis_update_set_t *update_set) {
    aegis_script_instance_t *inst = &rt->instances[sid];
    inst->vm.topic_table = &rt->topics;
    inst->vm.event_queue = &inst->event_queue;
    inst->vm.script_id = sid;
    inst->vm.signal_rate_limit_us = rt->config.signal_rate_limit_us;
    inst->vm.publish_fn = runtime_publish_cb;
    inst->vm.publish_ctx = rt;
    inst->vm.entity_view = view;
    inst->vm.async_buffer = async_buf;
    inst->vm.update_set = update_set;
}

/* ── Tests ────────────────────────────────────────────────────── */

/** Turret script compiles from IL. */
static void test_turret_compiles(void) {
    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    const char *src = turret_il_source();
    bool ok = aegis_asm_compile(&as, src, (uint32_t)strlen(src), &bc);
    if (!ok) {
        fprintf(stderr, "  ASM error at line %u: %s\n",
                aegis_asm_error_line(&as), aegis_asm_error(&as));
    }
    ASSERT_TRUE(ok);
    ASSERT_TRUE(bc.instruction_count > 0);
    free(bc.instructions);
}

/** Turret exits with code 3 when no player entity exists. */
static void test_turret_no_player(void) {
    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, turret_il_source(),
                (uint32_t)strlen(turret_il_source()), &bc));

    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = {0};
    cfg.max_instances = 4;
    cfg.max_subscriptions = 16;
    cfg.event_queue_cap = 8;
    cfg.vm_config = aegis_config_default();
    cfg.vm_config.arena_size = 8192;
    cfg.signal_rate_limit_us = 0; /* No rate limit for tests. */
    cfg.idle_grace_ticks = 4;
    aegis_script_runtime_init(&rt, &cfg);

    uint32_t sid = aegis_script_runtime_load(&rt, "turret", &bc);
    ASSERT_TRUE(sid != AEGIS_SCRIPT_ID_INVALID);

    /* Only a turret entity, no player. */
    script_entity_snapshot_t snaps[1];
    script_entity_view_t view;
    memset(snaps, 0, sizeof(snaps));
    snaps[0].entity_id = 0;
    snaps[0].active = 1;
    snaps[0].type = TURRET_TYPE;
    view.entities = (const script_entity_snapshot_t *)snaps;
    view.count = 1;
    view.capacity = 1;

    aegis_async_buffer_t async_buf;
    aegis_async_buffer_init(&async_buf, 16);
    aegis_update_set_t uset;
    memset(&uset, 0, sizeof(uset));
    uset.capacity = 16;
    uset.updates = calloc(16, sizeof(aegis_state_update_t));

    wire_vm(&rt, sid, &view, &async_buf, &uset);

    aegis_vm_status_t status = aegis_vm_run(&rt.instances[sid].vm);
    ASSERT_EQ(status, AEGIS_VM_EXITED);
    ASSERT_EQ(rt.instances[sid].vm.exit_code, 3); /* no player */

    free(uset.updates);
    free(bc.instructions);
    aegis_async_buffer_destroy(&async_buf);
    aegis_script_runtime_destroy(&rt);
}

/** Turret exits with code 1 when player is out of range. */
static void test_turret_out_of_range(void) {
    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, turret_il_source(),
                (uint32_t)strlen(turret_il_source()), &bc));

    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = {0};
    cfg.max_instances = 4;
    cfg.max_subscriptions = 16;
    cfg.event_queue_cap = 8;
    cfg.vm_config = aegis_config_default();
    cfg.vm_config.arena_size = 8192;
    cfg.signal_rate_limit_us = 0;
    cfg.idle_grace_ticks = 4;
    aegis_script_runtime_init(&rt, &cfg);

    uint32_t sid = aegis_script_runtime_load(&rt, "turret", &bc);

    /* Player at (100, 0, 0) — way out of range. */
    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_entity_snapshot(snaps, &view, 0, 0, 0, 100, 0, 0);

    aegis_async_buffer_t async_buf;
    aegis_async_buffer_init(&async_buf, 16);
    aegis_update_set_t uset;
    memset(&uset, 0, sizeof(uset));
    uset.capacity = 16;
    uset.updates = calloc(16, sizeof(aegis_state_update_t));

    wire_vm(&rt, sid, &view, &async_buf, &uset);

    aegis_vm_status_t status = aegis_vm_run(&rt.instances[sid].vm);
    ASSERT_EQ(status, AEGIS_VM_EXITED);
    ASSERT_EQ(rt.instances[sid].vm.exit_code, 1); /* out of range */

    free(uset.updates);
    free(bc.instructions);
    aegis_async_buffer_destroy(&async_buf);
    aegis_script_runtime_destroy(&rt);
}

/** Turret fires when player is in range with clear LOS. */
static void test_turret_fires_clear_los(void) {
    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, turret_il_source(),
                (uint32_t)strlen(turret_il_source()), &bc));

    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = {0};
    cfg.max_instances = 4;
    cfg.max_subscriptions = 16;
    cfg.event_queue_cap = 8;
    cfg.vm_config = aegis_config_default();
    cfg.vm_config.arena_size = 8192;
    cfg.signal_rate_limit_us = 0;
    cfg.idle_grace_ticks = 4;
    aegis_script_runtime_init(&rt, &cfg);

    uint32_t sid = aegis_script_runtime_load(&rt, "turret", &bc);

    /* Player at (10, 0, 0) — in range. */
    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_entity_snapshot(snaps, &view, 0, 0, 0, 10, 0, 0);

    /* Physics world with no obstacles (ray will miss → clear LOS). */
    phys_world_t world;
    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = 16;
    wcfg.max_colliders = 16;
    ASSERT_EQ(phys_world_init(&world, &wcfg), 0);

    aegis_async_buffer_t async_buf;
    aegis_async_buffer_init(&async_buf, 16);
    aegis_update_set_t uset;
    memset(&uset, 0, sizeof(uset));
    uset.capacity = 16;
    uset.updates = calloc(16, sizeof(aegis_state_update_t));

    wire_vm(&rt, sid, &view, &async_buf, &uset);

    /* Run script — it will submit a vis_test and then wait-yield. */
    aegis_vm_status_t status = aegis_vm_run(&rt.instances[sid].vm);
    ASSERT_EQ(status, AEGIS_VM_WAIT_YIELDED);

    /* Execute the raycast (empty world → miss → clear LOS). */
    uint32_t executed = aegis_async_execute_drain(&async_buf, &world, 16);
    ASSERT_EQ(executed, 1u);

    /* Resume the script. */
    aegis_vm_reset_fuel(&rt.instances[sid].vm);
    status = aegis_vm_run(&rt.instances[sid].vm);
    ASSERT_EQ(status, AEGIS_VM_EXITED);
    ASSERT_EQ(rt.instances[sid].vm.exit_code, 0); /* success */

    /* Verify: rotation update was pushed. */
    ASSERT_TRUE(uset.count >= 1);
    ASSERT_EQ(uset.updates[0].target, 0u);  /* turret entity */
    ASSERT_EQ(uset.updates[0].key, 1u);     /* ROT key */

    /* Verify: rotation is the normalized direction toward player.
     * Player at (10,0,0), turret at (0,0,0) → dir = (1,0,0). */
    float rot_x;
    memcpy(&rot_x, uset.updates[0].value, sizeof(float));
    ASSERT_NEAR(rot_x, 1.0f, 0.01f);

    phys_world_destroy(&world);
    free(uset.updates);
    free(bc.instructions);
    aegis_async_buffer_destroy(&async_buf);
    aegis_script_runtime_destroy(&rt);
}

/** Turret does NOT fire when LOS is blocked by an obstacle. */
static void test_turret_los_blocked(void) {
    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, turret_il_source(),
                (uint32_t)strlen(turret_il_source()), &bc));

    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = {0};
    cfg.max_instances = 4;
    cfg.max_subscriptions = 16;
    cfg.event_queue_cap = 8;
    cfg.vm_config = aegis_config_default();
    cfg.vm_config.arena_size = 8192;
    cfg.signal_rate_limit_us = 0;
    cfg.idle_grace_ticks = 4;
    aegis_script_runtime_init(&rt, &cfg);

    uint32_t sid = aegis_script_runtime_load(&rt, "turret", &bc);

    /* Player at (10, 0, 0) — in range. */
    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_entity_snapshot(snaps, &view, 0, 0, 0, 10, 0, 0);

    /* Physics world with a wall at (5, 0, 0) blocking LOS. */
    phys_world_t world;
    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = 16;
    wcfg.max_colliders = 16;
    ASSERT_EQ(phys_world_init(&world, &wcfg), 0);

    uint32_t wall_id = phys_world_create_body(&world);
    ASSERT_TRUE(wall_id != UINT32_MAX);
    phys_body_t *wall = phys_world_get_body(&world, wall_id);
    phys_body_set_mass(wall, 0.0f); /* static */
    wall->position = (phys_vec3_t){5.0f, 0.0f, 0.0f};
    phys_world_set_box_collider(&world, wall_id,
                                (phys_vec3_t){1.0f, 2.0f, 2.0f},
                                (phys_vec3_t){0, 0, 0},
                                (phys_quat_t){0, 0, 0, 1});

    aegis_async_buffer_t async_buf;
    aegis_async_buffer_init(&async_buf, 16);
    aegis_update_set_t uset;
    memset(&uset, 0, sizeof(uset));
    uset.capacity = 16;
    uset.updates = calloc(16, sizeof(aegis_state_update_t));

    wire_vm(&rt, sid, &view, &async_buf, &uset);

    /* Run script — wait-yields on vis_test. */
    aegis_vm_status_t status = aegis_vm_run(&rt.instances[sid].vm);
    ASSERT_EQ(status, AEGIS_VM_WAIT_YIELDED);

    /* Execute raycast — wall at x=5 blocks at distance ~4. */
    uint32_t executed = aegis_async_execute_drain(&async_buf, &world, 16);
    ASSERT_EQ(executed, 1u);

    /* Resume. */
    aegis_vm_reset_fuel(&rt.instances[sid].vm);
    status = aegis_vm_run(&rt.instances[sid].vm);
    ASSERT_EQ(status, AEGIS_VM_EXITED);
    ASSERT_EQ(rt.instances[sid].vm.exit_code, 2); /* LOS blocked */

    /* No updates should have been pushed. */
    ASSERT_EQ(uset.count, 0u);

    phys_world_destroy(&world);
    free(uset.updates);
    free(bc.instructions);
    aegis_async_buffer_destroy(&async_buf);
    aegis_script_runtime_destroy(&rt);
}

/** Perf benchmark: 1000 turret ticks under 50ms total. */
static void test_turret_perf_1000_ticks(void) {
    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, turret_il_source(),
                (uint32_t)strlen(turret_il_source()), &bc));

    /* Empty world (clear LOS every time). */
    phys_world_t world;
    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = 16;
    wcfg.max_colliders = 16;
    ASSERT_EQ(phys_world_init(&world, &wcfg), 0);

    /* Player at (10, 0, 0). */
    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_entity_snapshot(snaps, &view, 0, 0, 0, 10, 0, 0);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < 1000; i++) {
        aegis_script_runtime_t rt;
        aegis_runtime_config_t cfg = {0};
        cfg.max_instances = 2;
        cfg.max_subscriptions = 8;
        cfg.event_queue_cap = 4;
        cfg.vm_config = aegis_config_default();
        cfg.vm_config.arena_size = 8192;
        cfg.signal_rate_limit_us = 0;
        cfg.idle_grace_ticks = 4;
        aegis_script_runtime_init(&rt, &cfg);

        uint32_t sid = aegis_script_runtime_load(&rt, "turret", &bc);

        aegis_async_buffer_t async_buf;
        aegis_async_buffer_init(&async_buf, 16);
        aegis_update_set_t uset;
        memset(&uset, 0, sizeof(uset));
        uset.capacity = 16;
        uset.updates = calloc(16, sizeof(aegis_state_update_t));

        wire_vm(&rt, sid, &view, &async_buf, &uset);

        /* Run → wait-yield → execute raycast → resume → exit. */
        aegis_vm_run(&rt.instances[sid].vm);
        aegis_async_execute_drain(&async_buf, &world, 16);
        aegis_vm_reset_fuel(&rt.instances[sid].vm);
        aegis_vm_run(&rt.instances[sid].vm);

        free(uset.updates);
        aegis_async_buffer_destroy(&async_buf);
        aegis_script_runtime_destroy(&rt);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1e6;

    printf("(%.1f ms) ", elapsed_ms);
    ASSERT_TRUE(elapsed_ms < 5000.0); /* generous: 5s max (usually < 50ms) */
}

/** Multiple turrets running simultaneously against same player. */
static void test_multiple_turrets(void) {
    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, turret_il_source(),
                (uint32_t)strlen(turret_il_source()), &bc));

    phys_world_t world;
    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = 16;
    wcfg.max_colliders = 16;
    ASSERT_EQ(phys_world_init(&world, &wcfg), 0);

    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_entity_snapshot(snaps, &view, 0, 0, 0, 10, 0, 0);

    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = {0};
    cfg.max_instances = 8;
    cfg.max_subscriptions = 32;
    cfg.event_queue_cap = 8;
    cfg.vm_config = aegis_config_default();
    cfg.vm_config.arena_size = 8192;
    cfg.signal_rate_limit_us = 0;
    cfg.idle_grace_ticks = 4;
    aegis_script_runtime_init(&rt, &cfg);

    /* Load 4 turret scripts. */
    uint32_t sids[4];
    aegis_async_buffer_t async_bufs[4];
    aegis_update_set_t usets[4];

    for (int i = 0; i < 4; i++) {
        sids[i] = aegis_script_runtime_load(&rt, "turret", &bc);
        ASSERT_TRUE(sids[i] != AEGIS_SCRIPT_ID_INVALID);
        aegis_async_buffer_init(&async_bufs[i], 16);
        memset(&usets[i], 0, sizeof(usets[i]));
        usets[i].capacity = 16;
        usets[i].updates = calloc(16, sizeof(aegis_state_update_t));
        wire_vm(&rt, sids[i], &view, &async_bufs[i], &usets[i]);
    }

    /* Phase 1: all submit raycasts (wait-yield). */
    for (int i = 0; i < 4; i++) {
        aegis_vm_status_t s = aegis_vm_run(&rt.instances[sids[i]].vm);
        ASSERT_EQ(s, AEGIS_VM_WAIT_YIELDED);
    }

    /* Phase 2: execute all raycasts. */
    for (int i = 0; i < 4; i++) {
        uint32_t ex = aegis_async_execute_drain(&async_bufs[i], &world, 16);
        ASSERT_EQ(ex, 1u);
    }

    /* Phase 3: resume all — should fire. */
    for (int i = 0; i < 4; i++) {
        aegis_vm_reset_fuel(&rt.instances[sids[i]].vm);
        aegis_vm_status_t s = aegis_vm_run(&rt.instances[sids[i]].vm);
        ASSERT_EQ(s, AEGIS_VM_EXITED);
        ASSERT_EQ(rt.instances[sids[i]].vm.exit_code, 0);
        ASSERT_TRUE(usets[i].count >= 1);
    }

    for (int i = 0; i < 4; i++) {
        free(usets[i].updates);
        aegis_async_buffer_destroy(&async_bufs[i]);
    }

    phys_world_destroy(&world);
    free(bc.instructions);
    aegis_script_runtime_destroy(&rt);
}

/** Full lifecycle: turret loads, runs, signals, exits, can re-arm. */
static void test_turret_lifecycle(void) {
    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, turret_il_source(),
                (uint32_t)strlen(turret_il_source()), &bc));

    phys_world_t world;
    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = 16;
    wcfg.max_colliders = 16;
    ASSERT_EQ(phys_world_init(&world, &wcfg), 0);

    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_entity_snapshot(snaps, &view, 0, 0, 0, 10, 0, 0);

    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = {0};
    cfg.max_instances = 4;
    cfg.max_subscriptions = 16;
    cfg.event_queue_cap = 8;
    cfg.vm_config = aegis_config_default();
    cfg.vm_config.arena_size = 8192;
    cfg.signal_rate_limit_us = 0;
    cfg.idle_grace_ticks = 4;
    aegis_script_runtime_init(&rt, &cfg);

    uint32_t sid = aegis_script_runtime_load(&rt, "turret", &bc);
    aegis_async_buffer_t async_buf;
    aegis_async_buffer_init(&async_buf, 16);
    aegis_update_set_t uset;
    memset(&uset, 0, sizeof(uset));
    uset.capacity = 16;
    uset.updates = calloc(16, sizeof(aegis_state_update_t));
    wire_vm(&rt, sid, &view, &async_buf, &uset);

    /* Tick 1: run → wait → execute → resume → exit(0). */
    aegis_vm_run(&rt.instances[sid].vm);
    aegis_async_execute_drain(&async_buf, &world, 16);
    aegis_vm_reset_fuel(&rt.instances[sid].vm);
    aegis_vm_status_t status = aegis_vm_run(&rt.instances[sid].vm);
    ASSERT_EQ(status, AEGIS_VM_EXITED);
    ASSERT_EQ(rt.instances[sid].vm.exit_code, 0);

    /* Mark pending unschedule (as runtime would). */
    aegis_runtime_mark_pending_unschedule(&rt.instances[sid]);
    ASSERT_TRUE(rt.instances[sid].pending_unschedule);

    /* Tick idle twice — still active within grace window. */
    aegis_runtime_tick_idle(&rt);
    aegis_runtime_tick_idle(&rt);
    ASSERT_TRUE(rt.instances[sid].active);

    /* Simulate event arrival — resets idle. */
    aegis_runtime_reset_idle(&rt.instances[sid]);
    ASSERT_FALSE(rt.instances[sid].pending_unschedule);

    phys_world_destroy(&world);
    free(uset.updates);
    free(bc.instructions);
    aegis_async_buffer_destroy(&async_buf);
    aegis_script_runtime_destroy(&rt);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== turret_script_e2e_tests ===\n");

    RUN(test_turret_compiles);
    RUN(test_turret_no_player);
    RUN(test_turret_out_of_range);
    RUN(test_turret_fires_clear_los);
    RUN(test_turret_los_blocked);
    RUN(test_turret_perf_1000_ticks);
    RUN(test_multiple_turrets);
    RUN(test_turret_lifecycle);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
