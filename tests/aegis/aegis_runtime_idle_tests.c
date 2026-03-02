/**
 * @file aegis_runtime_idle_tests.c
 * @brief Tests for exit-driven idle tracking and unscheduling.
 *
 * Covers: mark pending unschedule on exit, idle counter increments,
 * grace window reset on event, unschedule on expiry, config defaults,
 * and interaction with script lifecycle.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/aegis/aegis_runtime.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                       */
/* ------------------------------------------------------------------ */

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n", #fn); } while (0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } \
    g_pass++; } while (0)

#define ASSERT_FALSE(expr) do { \
    if ((expr)) { printf("FAIL %s:%d: !(%s)\n", __FILE__, __LINE__, #expr); g_fail++; return; } \
    g_pass++; } while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { printf("FAIL %s:%d: %d != %d\n", __FILE__, __LINE__, (int)(a), (int)(b)); g_fail++; return; } \
    g_pass++; } while (0)

/* Forward declarations for runtime idle API. */
void aegis_runtime_mark_pending_unschedule(aegis_script_instance_t *inst);
void aegis_runtime_tick_idle(aegis_script_runtime_t *rt);
void aegis_runtime_reset_idle(aegis_script_instance_t *inst);
bool aegis_runtime_is_pending_unschedule(const aegis_script_instance_t *inst);

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void init_test_runtime(aegis_script_runtime_t *rt) {
    aegis_runtime_config_t cfg = {0};
    cfg.max_instances = 4;
    cfg.max_subscriptions = 16;
    cfg.event_queue_cap = 8;
    cfg.vm_config = aegis_config_default();
    cfg.vm_config.arena_size = 4096;
    cfg.vm_config.static_max = 256;
    cfg.vm_config.stack_max = 256;
    cfg.signal_rate_limit_us = 250;
    cfg.idle_grace_ticks = 4;
    aegis_script_runtime_init(rt, &cfg);
}

static uint32_t load_noop_script(aegis_script_runtime_t *rt, const char *name) {
    /* A bytecode with just an EXIT instruction. */
    aegis_instruction_t insn = {0};
    insn.words[0] = AEGIS_OP_EXIT; /* opcode = EXIT */
    aegis_bytecode_t bc = {0};
    bc.instructions = &insn;
    bc.instruction_count = 1;
    return aegis_script_runtime_load(rt, name, &bc);
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_mark_pending_unschedule(void) {
    aegis_script_runtime_t rt;
    init_test_runtime(&rt);

    uint32_t sid = load_noop_script(&rt, "test_script");
    ASSERT_TRUE(sid != AEGIS_SCRIPT_ID_INVALID);

    aegis_script_instance_t *inst = &rt.instances[sid];
    ASSERT_FALSE(inst->pending_unschedule);

    aegis_runtime_mark_pending_unschedule(inst);
    ASSERT_TRUE(inst->pending_unschedule);
    ASSERT_EQ(inst->idle_ticks, 0);

    aegis_script_runtime_destroy(&rt);
}

static void test_tick_idle_increments(void) {
    aegis_script_runtime_t rt;
    init_test_runtime(&rt);

    uint32_t sid = load_noop_script(&rt, "test_script");
    aegis_script_instance_t *inst = &rt.instances[sid];

    aegis_runtime_mark_pending_unschedule(inst);
    ASSERT_EQ(inst->idle_ticks, 0);

    aegis_runtime_tick_idle(&rt);
    ASSERT_EQ(inst->idle_ticks, 1);

    aegis_runtime_tick_idle(&rt);
    ASSERT_EQ(inst->idle_ticks, 2);

    aegis_script_runtime_destroy(&rt);
}

static void test_idle_unschedules_on_expiry(void) {
    aegis_script_runtime_t rt;
    init_test_runtime(&rt);
    /* Grace = 4 ticks. */

    uint32_t sid = load_noop_script(&rt, "test_script");
    aegis_script_instance_t *inst = &rt.instances[sid];

    aegis_runtime_mark_pending_unschedule(inst);

    /* Tick 4 times — should still be active (grace = 4, need > 4 to expire). */
    for (int i = 0; i < 4; i++) {
        aegis_runtime_tick_idle(&rt);
        ASSERT_TRUE(inst->active);
    }

    /* 5th tick → exceeds grace → unscheduled. */
    aegis_runtime_tick_idle(&rt);
    ASSERT_FALSE(inst->active);
    ASSERT_FALSE(inst->pending_unschedule);

    aegis_script_runtime_destroy(&rt);
}

static void test_reset_idle_on_event(void) {
    aegis_script_runtime_t rt;
    init_test_runtime(&rt);

    uint32_t sid = load_noop_script(&rt, "test_script");
    aegis_script_instance_t *inst = &rt.instances[sid];

    aegis_runtime_mark_pending_unschedule(inst);

    /* Tick a couple times. */
    aegis_runtime_tick_idle(&rt);
    aegis_runtime_tick_idle(&rt);
    ASSERT_EQ(inst->idle_ticks, 2);

    /* Reset (simulates event arrival). */
    aegis_runtime_reset_idle(inst);
    ASSERT_FALSE(inst->pending_unschedule);
    ASSERT_EQ(inst->idle_ticks, 0);

    /* Should no longer be affected by tick_idle. */
    aegis_runtime_tick_idle(&rt);
    ASSERT_TRUE(inst->active);

    aegis_script_runtime_destroy(&rt);
}

static void test_non_pending_not_affected(void) {
    aegis_script_runtime_t rt;
    init_test_runtime(&rt);

    uint32_t sid = load_noop_script(&rt, "test_script");
    aegis_script_instance_t *inst = &rt.instances[sid];

    /* Don't mark pending — tick_idle should not affect it. */
    for (int i = 0; i < 20; i++) {
        aegis_runtime_tick_idle(&rt);
    }
    ASSERT_TRUE(inst->active);
    ASSERT_EQ(inst->idle_ticks, 0);

    aegis_script_runtime_destroy(&rt);
}

static void test_is_pending_unschedule(void) {
    aegis_script_runtime_t rt;
    init_test_runtime(&rt);

    uint32_t sid = load_noop_script(&rt, "test_script");
    aegis_script_instance_t *inst = &rt.instances[sid];

    ASSERT_FALSE(aegis_runtime_is_pending_unschedule(inst));
    aegis_runtime_mark_pending_unschedule(inst);
    ASSERT_TRUE(aegis_runtime_is_pending_unschedule(inst));
    aegis_runtime_reset_idle(inst);
    ASSERT_FALSE(aegis_runtime_is_pending_unschedule(inst));

    aegis_script_runtime_destroy(&rt);
}

static void test_multiple_scripts_idle(void) {
    aegis_script_runtime_t rt;
    init_test_runtime(&rt);

    uint32_t s0 = load_noop_script(&rt, "script_a");
    uint32_t s1 = load_noop_script(&rt, "script_b");

    aegis_runtime_mark_pending_unschedule(&rt.instances[s0]);
    /* s1 stays active and NOT pending. */

    for (int i = 0; i < 10; i++) {
        aegis_runtime_tick_idle(&rt);
    }

    /* s0 should be unscheduled, s1 should be fine. */
    ASSERT_FALSE(rt.instances[s0].active);
    ASSERT_TRUE(rt.instances[s1].active);

    aegis_script_runtime_destroy(&rt);
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== aegis_runtime_idle_tests ===\n");

    RUN(test_mark_pending_unschedule);
    RUN(test_tick_idle_increments);
    RUN(test_idle_unschedules_on_expiry);
    RUN(test_reset_idle_on_event);
    RUN(test_non_pending_not_affected);
    RUN(test_is_pending_unschedule);
    RUN(test_multiple_scripts_idle);

    printf("\nRESULTS: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
