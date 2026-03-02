/**
 * @file aegis_signal_integration_tests.c
 * @brief End-to-end integration tests for event signaling pipeline.
 *
 * Tests the full flow: script signals → topic routes → subscriber
 * awaits → receives event. Uses assembled bytecode and the runtime.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/aegis/aegis_runtime.h"
#include "ferrum/aegis/aegis_ops_signal.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/aegis/aegis_asm.h"
#include "ferrum/aegis/aegis_decode.h"

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

/* Forward declare idle API. */
void aegis_runtime_mark_pending_unschedule(aegis_script_instance_t *inst);
void aegis_runtime_tick_idle(aegis_script_runtime_t *rt);
void aegis_runtime_reset_idle(aegis_script_instance_t *inst);

/* ------------------------------------------------------------------ */
/* Publish callback adapter                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Publish callback that routes through the runtime.
 */
static void runtime_publish_cb(void *ctx, const aegis_event_t *ev) {
    aegis_script_runtime_publish((aegis_script_runtime_t *)ctx, ev);
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void init_test_runtime(aegis_script_runtime_t *rt) {
    aegis_runtime_config_t cfg = {0};
    cfg.max_instances = 8;
    cfg.max_subscriptions = 32;
    cfg.event_queue_cap = 16;
    cfg.vm_config = aegis_config_default();
    cfg.vm_config.arena_size = 4096;
    cfg.vm_config.static_max = 256;
    cfg.vm_config.stack_max = 256;
    cfg.signal_rate_limit_us = 250;
    cfg.idle_grace_ticks = 4;
    aegis_script_runtime_init(rt, &cfg);
}

/* ------------------------------------------------------------------ */
/* Integration tests                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Signal → route → await round-trip.
 *
 * Script A signals topic 100.
 * Script B subscribes to topic 100 and awaits.
 * Verify B receives the event after A signals.
 */
static void test_signal_route_await_roundtrip(void) {
    aegis_script_runtime_t rt;
    init_test_runtime(&rt);

    /* Script A: signals topic 100, then exits. */
    const char *src_a =
        "load_imm r1 100\n"
        "load_imm r2 42\n"      /* payload = 42 */
        "signal r0 r1 r2\n"
        "exit r0\n";

    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc_a;
    ASSERT_TRUE(aegis_asm_compile(&as, src_a, (uint32_t)strlen(src_a), &bc_a));

    uint32_t sid_a = aegis_script_runtime_load(&rt, "signaler", &bc_a);
    ASSERT_TRUE(sid_a != AEGIS_SCRIPT_ID_INVALID);

    /* Script B: subscribes to topic 100, then awaits. */
    const char *src_b =
        "load_imm r1 100\n"
        "subscribe r0 r1\n"
        "await_event r2 r1\n"
        "exit r2\n";

    aegis_asm_init(&as);
    aegis_bytecode_t bc_b;
    ASSERT_TRUE(aegis_asm_compile(&as, src_b, (uint32_t)strlen(src_b), &bc_b));

    uint32_t sid_b = aegis_script_runtime_load(&rt, "listener", &bc_b);
    ASSERT_TRUE(sid_b != AEGIS_SCRIPT_ID_INVALID);

    /* Wire up VM contexts for direct execution (no fibers). */
    aegis_script_instance_t *inst_a = &rt.instances[sid_a];
    aegis_script_instance_t *inst_b = &rt.instances[sid_b];

    inst_a->vm.topic_table = &rt.topics;
    inst_a->vm.event_queue = &inst_a->event_queue;
    inst_a->vm.script_id = sid_a;
    inst_a->vm.signal_rate_limit_us = rt.config.signal_rate_limit_us;
    inst_a->vm.publish_fn = runtime_publish_cb;
    inst_a->vm.publish_ctx = &rt;

    inst_b->vm.topic_table = &rt.topics;
    inst_b->vm.event_queue = &inst_b->event_queue;
    inst_b->vm.script_id = sid_b;
    inst_b->vm.signal_rate_limit_us = rt.config.signal_rate_limit_us;
    inst_b->vm.publish_fn = runtime_publish_cb;
    inst_b->vm.publish_ctx = &rt;

    /* Step 1: Run script B to subscribe + start await (will wait-yield). */
    aegis_vm_status_t status_b = aegis_vm_run(&inst_b->vm);
    ASSERT_EQ(status_b, AEGIS_VM_WAIT_YIELDED);

    /* Step 2: Run script A to signal event. */
    aegis_vm_status_t status_a = aegis_vm_run(&inst_a->vm);
    ASSERT_EQ(status_a, AEGIS_VM_EXITED);
    ASSERT_EQ(inst_a->vm.exit_code, 0); /* signal succeeded */

    /* Step 3: Resume script B — should now find the event. */
    aegis_vm_reset_fuel(&inst_b->vm);
    status_b = aegis_vm_run(&inst_b->vm);
    ASSERT_EQ(status_b, AEGIS_VM_EXITED);
    ASSERT_EQ(inst_b->vm.regs[2].u32, 100); /* event type */

    free(bc_a.instructions);
    free(bc_b.instructions);
    aegis_script_runtime_destroy(&rt);
}

/**
 * @brief Exit → pending unschedule → event resets idle → survives.
 */
static void test_exit_pending_then_event_resets(void) {
    aegis_script_runtime_t rt;
    init_test_runtime(&rt);

    aegis_instruction_t insn = {0};
    insn.words[0] = AEGIS_OP_EXIT;
    aegis_bytecode_t bc = {0};
    bc.instructions = &insn;
    bc.instruction_count = 1;
    bc.topic_hash = 42;

    uint32_t sid = aegis_script_runtime_load(&rt, "test", &bc);
    ASSERT_TRUE(sid != AEGIS_SCRIPT_ID_INVALID);

    aegis_script_instance_t *inst = &rt.instances[sid];

    /* Simulate: script exited, mark pending. */
    aegis_runtime_mark_pending_unschedule(inst);
    ASSERT_TRUE(inst->pending_unschedule);

    /* Tick twice. */
    aegis_runtime_tick_idle(&rt);
    aegis_runtime_tick_idle(&rt);
    ASSERT_EQ(inst->idle_ticks, 2);
    ASSERT_TRUE(inst->active);

    /* Event arrives — reset idle. */
    aegis_runtime_reset_idle(inst);
    ASSERT_FALSE(inst->pending_unschedule);
    ASSERT_EQ(inst->idle_ticks, 0);

    /* Tick many more times — should NOT unschedule since not pending. */
    for (int i = 0; i < 20; i++) {
        aegis_runtime_tick_idle(&rt);
    }
    ASSERT_TRUE(inst->active);

    aegis_script_runtime_destroy(&rt);
}

/**
 * @brief Exit → no event → unschedule after grace window.
 */
static void test_exit_no_event_unschedules(void) {
    aegis_script_runtime_t rt;
    init_test_runtime(&rt);

    aegis_instruction_t insn = {0};
    insn.words[0] = AEGIS_OP_EXIT;
    aegis_bytecode_t bc = {0};
    bc.instructions = &insn;
    bc.instruction_count = 1;

    uint32_t sid = aegis_script_runtime_load(&rt, "test", &bc);
    aegis_script_instance_t *inst = &rt.instances[sid];

    aegis_runtime_mark_pending_unschedule(inst);

    /* Grace = 4. Tick 5 times → unschedule. */
    for (int i = 0; i < 5; i++) {
        aegis_runtime_tick_idle(&rt);
    }
    ASSERT_FALSE(inst->active);

    aegis_script_runtime_destroy(&rt);
}

/**
 * @brief Rate-limited signal correctly throttles rapid signals.
 */
static void test_rate_limiting_end_to_end(void) {
    aegis_script_runtime_t rt;
    init_test_runtime(&rt);

    /* Script signals three times rapidly. */
    const char *src =
        "load_imm r1 50\n"
        "load_imm r2 0\n"
        "signal r3 r1 r2\n"      /* 1st signal → r3 */
        "signal r4 r1 r2\n"      /* 2nd signal → r4 (rate-limited) */
        "signal r5 r1 r2\n"      /* 3rd signal → r5 (rate-limited) */
        "exit r4\n";             /* exit with r4's status */

    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, src, (uint32_t)strlen(src), &bc));

    uint32_t sid = aegis_script_runtime_load(&rt, "rapid_signaler", &bc);
    aegis_script_instance_t *inst = &rt.instances[sid];

    inst->vm.topic_table = &rt.topics;
    inst->vm.event_queue = &inst->event_queue;
    inst->vm.script_id = sid;
    inst->vm.signal_rate_limit_us = 250;
    inst->vm.publish_fn = runtime_publish_cb;
    inst->vm.publish_ctx = &rt;

    /* Instructions execute in nanoseconds — all signals happen within
     * the rate limit window, so 2nd and 3rd are rate-limited. */
    aegis_vm_status_t status = aegis_vm_run(&inst->vm);
    ASSERT_EQ(status, AEGIS_VM_EXITED);
    ASSERT_EQ(inst->vm.exit_code, 1); /* r4 = rate-limited status */
    ASSERT_EQ(inst->vm.regs[3].i32, 0); /* r3 = first signal succeeded */
    ASSERT_EQ(inst->vm.regs[5].i32, 1); /* r5 = also rate-limited */

    free(bc.instructions);
    aegis_script_runtime_destroy(&rt);
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== aegis_signal_integration_tests ===\n");

    RUN(test_signal_route_await_roundtrip);
    RUN(test_exit_pending_then_event_resets);
    RUN(test_exit_no_event_unschedules);
    RUN(test_rate_limiting_end_to_end);

    printf("\nRESULTS: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
