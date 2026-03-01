/**
 * @file aegis_runtime_tests.c
 * @brief Tests for Aegis script runtime: load/unload, event routing, fiber execution.
 *
 * Uses the real job system for fiber-based execution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "ferrum/aegis/aegis_runtime.h"
#include "ferrum/aegis/aegis_asm.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/job/system.h"

/* ======================================================================= */
/* Test harness                                                             */
/* ======================================================================= */

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT_EQ(expected, actual) do { \
    int _e = (expected), _a = (actual); \
    if (_e != _a) { \
        printf("  FAIL %s:%d: expected %d, got %d\n", \
               __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

#define ASSERT_UINT_EQ(expected, actual) do { \
    uint32_t _e = (expected), _a = (actual); \
    if (_e != _a) { \
        printf("  FAIL %s:%d: expected %u, got %u\n", \
               __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    int _r = fn(); \
    if (_r == 0) { printf("OK   %s\n", #fn); g_pass++; } \
    else { g_fail++; } \
} while (0)

/* ======================================================================= */
/* Helpers                                                                  */
/* ======================================================================= */

/** Compile IL source into bytecode. Caller must free bc->instructions. */
static bool compile_il(const char *src, aegis_bytecode_t *bc) {
    aegis_asm_t as;
    memset(&as, 0, sizeof(as));
    uint32_t len = (uint32_t)strlen(src);
    bool ok = aegis_asm_compile(&as, src, len, bc);
    return ok;
}

/** Create a default runtime config. */
static aegis_runtime_config_t default_rt_config(void) {
    aegis_runtime_config_t cfg;
    cfg.max_instances    = 16;
    cfg.max_subscriptions = 64;
    cfg.event_queue_cap  = 32;
    cfg.vm_config        = aegis_config_default();
    /* Use small fuel budget to test force-yield behavior. */
    cfg.vm_config.fuel_budget = 200;
    return cfg;
}

/* ======================================================================= */
/* Test: init and destroy                                                   */
/* ======================================================================= */

static int test_init_destroy(void) {
    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = default_rt_config();

    ASSERT_TRUE(aegis_script_runtime_init(&rt, &cfg));
    ASSERT_UINT_EQ(0, rt.instance_count);
    aegis_script_runtime_destroy(&rt);
    return 0;
}

/* ======================================================================= */
/* Test: load a script                                                      */
/* ======================================================================= */

static int test_load_script(void) {
    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = default_rt_config();
    ASSERT_TRUE(aegis_script_runtime_init(&rt, &cfg));

    /* Simple script: yield immediately, then exit on next resume. */
    const char *src =
        ".topic !test\n"
        "yield\n"
        "exit 0\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    uint32_t id = aegis_script_runtime_load(&rt, "test_script", &bc);
    ASSERT_TRUE(id != AEGIS_SCRIPT_ID_INVALID);
    ASSERT_UINT_EQ(1, rt.instance_count);

    /* Verify the instance is active. */
    ASSERT_TRUE(rt.instances[id].active);

    aegis_script_runtime_unload(&rt, id);
    ASSERT_TRUE(!rt.instances[id].active);

    free(bc.instructions);
    aegis_script_runtime_destroy(&rt);
    return 0;
}

/* ======================================================================= */
/* Test: topic auto-subscription on load                                    */
/* ======================================================================= */

static int test_topic_auto_subscribe(void) {
    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = default_rt_config();
    ASSERT_TRUE(aegis_script_runtime_init(&rt, &cfg));

    const char *src =
        ".topic !hit\n"
        "yield\n"
        "exit 0\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    uint32_t id = aegis_script_runtime_load(&rt, "hit_handler", &bc);
    ASSERT_TRUE(id != AEGIS_SCRIPT_ID_INVALID);

    /* Push an event with the !hit topic hash. */
    uint32_t hit_hash = aegis_topic_hash("!hit");
    aegis_event_t ev = {0};
    ev.type = hit_hash;
    ev.source = 42;
    ev.tick = 1;

    /* Publish through the runtime's topic table.
     * Should route to the loaded script's queue. */
    aegis_script_runtime_publish(&rt, &ev);
    ASSERT_UINT_EQ(1, aegis_event_queue_count(&rt.instances[id].event_queue));

    /* Unload should unsubscribe — subsequent publishes shouldn't route. */
    aegis_script_runtime_unload(&rt, id);
    aegis_event_t ev2 = ev;
    ev2.tick = 2;
    aegis_script_runtime_publish(&rt, &ev2);
    /* Queue was destroyed by unload, so we just verify no crash. */

    free(bc.instructions);
    aegis_script_runtime_destroy(&rt);
    return 0;
}

/* ======================================================================= */
/* Test: load at capacity                                                   */
/* ======================================================================= */

static int test_load_at_capacity(void) {
    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = default_rt_config();
    cfg.max_instances = 2;
    ASSERT_TRUE(aegis_script_runtime_init(&rt, &cfg));

    const char *src = "exit 0\n";
    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    uint32_t id0 = aegis_script_runtime_load(&rt, "s0", &bc);
    uint32_t id1 = aegis_script_runtime_load(&rt, "s1", &bc);
    ASSERT_TRUE(id0 != AEGIS_SCRIPT_ID_INVALID);
    ASSERT_TRUE(id1 != AEGIS_SCRIPT_ID_INVALID);

    /* Third load should fail — at capacity. */
    uint32_t id2 = aegis_script_runtime_load(&rt, "s2", &bc);
    ASSERT_TRUE(id2 == AEGIS_SCRIPT_ID_INVALID);

    /* Unload one, then load should succeed again. */
    aegis_script_runtime_unload(&rt, id0);
    uint32_t id3 = aegis_script_runtime_load(&rt, "s3", &bc);
    ASSERT_TRUE(id3 != AEGIS_SCRIPT_ID_INVALID);

    free(bc.instructions);
    aegis_script_runtime_destroy(&rt);
    return 0;
}

/* ======================================================================= */
/* Test: fiber execution — script runs and exits                            */
/* ======================================================================= */

static int test_fiber_execution_exit(void) {
    /* Set up job system. */
    job_system_t sys = {0};
    job_system_create_status_t jstatus =
        job_system_create(&sys, 2, 64, 64 * 1024, 256, 0);
    ASSERT_TRUE(jstatus == JOB_CREATE_OK);
    ASSERT_INT_EQ(0, job_system_start(&sys));

    /* Set up runtime. */
    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = default_rt_config();
    cfg.vm_config.fuel_budget = 1000;
    ASSERT_TRUE(aegis_script_runtime_init(&rt, &cfg));

    /* Script: on receiving any event, store source entity in r0, exit. */
    const char *src =
        ".topic !go\n"
        "event_src r0\n"
        "exit 0\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    uint32_t id = aegis_script_runtime_load(&rt, "go_handler", &bc);
    ASSERT_TRUE(id != AEGIS_SCRIPT_ID_INVALID);

    /* Dispatch the script fiber. */
    aegis_script_runtime_start(&rt, id, &sys);

    /* Push an event to trigger execution. */
    aegis_event_t ev = {0};
    ev.type = aegis_topic_hash("!go");
    ev.source = 99;
    ev.tick = 1;
    aegis_script_runtime_publish(&rt, &ev);

    /* Wait for the fiber to complete. */
    job_system_wait_idle(&sys);

    /* Give the fiber time to finish (it should exit quickly). */
    /* Verify the script exited. */
    ASSERT_TRUE(!rt.instances[id].active);
    ASSERT_UINT_EQ(AEGIS_VM_EXITED, rt.instances[id].vm.status);
    ASSERT_UINT_EQ(0, rt.instances[id].vm.exit_code);

    free(bc.instructions);
    aegis_script_runtime_destroy(&rt);
    job_system_shutdown(&sys);
    return 0;
}

/* ======================================================================= */
/* Test: fiber execution — force-yield and resume                           */
/* ======================================================================= */

static int test_fiber_force_yield_resume(void) {
    job_system_t sys = {0};
    job_system_create_status_t jstatus =
        job_system_create(&sys, 2, 64, 64 * 1024, 256, 0);
    ASSERT_TRUE(jstatus == JOB_CREATE_OK);
    ASSERT_INT_EQ(0, job_system_start(&sys));

    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = default_rt_config();
    /* Very small fuel budget to force multiple yields. */
    cfg.vm_config.fuel_budget = 10;
    ASSERT_TRUE(aegis_script_runtime_init(&rt, &cfg));

    /* Script: count from 0 to 100 in a loop (will force-yield many times),
     * then store result in r10 and exit with the count. */
    const char *src =
        ".topic !start\n"
        "mov r0, 0\n"          /* counter */
        "mov r1, 1\n"          /* increment */
        "mov r2, 100\n"        /* limit */
        "loop:\n"
        "add r0, r0, r1\n"    /* counter++ */
        "lt r3, r0, r2\n"     /* r3 = counter < limit */
        "jmp_if r3, loop\n"   /* if not done, loop */
        "exit 0\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    uint32_t id = aegis_script_runtime_load(&rt, "counter", &bc);
    ASSERT_TRUE(id != AEGIS_SCRIPT_ID_INVALID);

    aegis_script_runtime_start(&rt, id, &sys);

    /* Push event to start execution. */
    aegis_event_t ev = {0};
    ev.type = aegis_topic_hash("!start");
    ev.source = 1;
    ev.tick = 1;
    aegis_script_runtime_publish(&rt, &ev);

    /* Wait for completion. The script will force-yield many times
     * (100 iterations / 10 fuel = ~10+ yields). */
    job_system_wait_idle(&sys);

    /* Verify script completed successfully. */
    ASSERT_TRUE(!rt.instances[id].active);
    ASSERT_UINT_EQ(AEGIS_VM_EXITED, rt.instances[id].vm.status);
    /* r0 should be 100. */
    ASSERT_UINT_EQ(100, rt.instances[id].vm.regs[0].u32);

    free(bc.instructions);
    aegis_script_runtime_destroy(&rt);
    job_system_shutdown(&sys);
    return 0;
}

/* ======================================================================= */
/* Test: multiple scripts run concurrently                                  */
/* ======================================================================= */

static int test_multiple_scripts_concurrent(void) {
    job_system_t sys = {0};
    job_system_create_status_t jstatus =
        job_system_create(&sys, 4, 128, 64 * 1024, 256, 0);
    ASSERT_TRUE(jstatus == JOB_CREATE_OK);
    ASSERT_INT_EQ(0, job_system_start(&sys));

    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = default_rt_config();
    cfg.vm_config.fuel_budget = 50;
    ASSERT_TRUE(aegis_script_runtime_init(&rt, &cfg));

    /* Script A: count to 50, exit with 0. */
    const char *src_a =
        ".topic !run_a\n"
        "mov r0, 0\n"
        "mov r1, 1\n"
        "mov r2, 50\n"
        "loop:\n"
        "add r0, r0, r1\n"
        "lt r3, r0, r2\n"
        "jmp_if r3, loop\n"
        "exit 0\n";

    /* Script B: count to 75, exit with 0. */
    const char *src_b =
        ".topic !run_b\n"
        "mov r0, 0\n"
        "mov r1, 1\n"
        "mov r2, 75\n"
        "loop:\n"
        "add r0, r0, r1\n"
        "lt r3, r0, r2\n"
        "jmp_if r3, loop\n"
        "exit 0\n";

    aegis_bytecode_t bc_a, bc_b;
    ASSERT_TRUE(compile_il(src_a, &bc_a));
    ASSERT_TRUE(compile_il(src_b, &bc_b));

    uint32_t id_a = aegis_script_runtime_load(&rt, "script_a", &bc_a);
    uint32_t id_b = aegis_script_runtime_load(&rt, "script_b", &bc_b);
    ASSERT_TRUE(id_a != AEGIS_SCRIPT_ID_INVALID);
    ASSERT_TRUE(id_b != AEGIS_SCRIPT_ID_INVALID);

    aegis_script_runtime_start(&rt, id_a, &sys);
    aegis_script_runtime_start(&rt, id_b, &sys);

    /* Push events for both scripts. */
    aegis_event_t ev_a = {0};
    ev_a.type = aegis_topic_hash("!run_a");
    ev_a.tick = 1;
    aegis_script_runtime_publish(&rt, &ev_a);

    aegis_event_t ev_b = {0};
    ev_b.type = aegis_topic_hash("!run_b");
    ev_b.tick = 1;
    aegis_script_runtime_publish(&rt, &ev_b);

    job_system_wait_idle(&sys);

    /* Both should have exited. */
    ASSERT_TRUE(!rt.instances[id_a].active);
    ASSERT_TRUE(!rt.instances[id_b].active);
    ASSERT_UINT_EQ(50, rt.instances[id_a].vm.regs[0].u32);
    ASSERT_UINT_EQ(75, rt.instances[id_b].vm.regs[0].u32);

    free(bc_a.instructions);
    free(bc_b.instructions);
    aegis_script_runtime_destroy(&rt);
    job_system_shutdown(&sys);
    return 0;
}

/* ======================================================================= */
/* Test: event routing — only subscribed scripts receive events              */
/* ======================================================================= */

static int test_event_routing_selective(void) {
    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = default_rt_config();
    ASSERT_TRUE(aegis_script_runtime_init(&rt, &cfg));

    const char *src_hit =
        ".topic !hit\n"
        "exit 0\n";
    const char *src_spawn =
        ".topic !spawn\n"
        "exit 0\n";

    aegis_bytecode_t bc_hit, bc_spawn;
    ASSERT_TRUE(compile_il(src_hit, &bc_hit));
    ASSERT_TRUE(compile_il(src_spawn, &bc_spawn));

    uint32_t id_hit   = aegis_script_runtime_load(&rt, "on_hit", &bc_hit);
    uint32_t id_spawn = aegis_script_runtime_load(&rt, "on_spawn", &bc_spawn);

    /* Publish a !hit event. */
    aegis_event_t ev = {0};
    ev.type = aegis_topic_hash("!hit");
    ev.source = 10;
    aegis_script_runtime_publish(&rt, &ev);

    /* Only the hit handler should have an event. */
    ASSERT_UINT_EQ(1, aegis_event_queue_count(&rt.instances[id_hit].event_queue));
    ASSERT_UINT_EQ(0, aegis_event_queue_count(&rt.instances[id_spawn].event_queue));

    /* Publish a !spawn event. */
    aegis_event_t ev2 = {0};
    ev2.type = aegis_topic_hash("!spawn");
    ev2.source = 20;
    aegis_script_runtime_publish(&rt, &ev2);

    ASSERT_UINT_EQ(1, aegis_event_queue_count(&rt.instances[id_hit].event_queue));
    ASSERT_UINT_EQ(1, aegis_event_queue_count(&rt.instances[id_spawn].event_queue));

    free(bc_hit.instructions);
    free(bc_spawn.instructions);
    aegis_script_runtime_destroy(&rt);
    return 0;
}

/* ======================================================================= */
/* Test: script with yield processes multiple events                        */
/* ======================================================================= */

static int test_yield_processes_multiple_events(void) {
    job_system_t sys = {0};
    job_system_create_status_t jstatus =
        job_system_create(&sys, 2, 64, 64 * 1024, 256, 0);
    ASSERT_TRUE(jstatus == JOB_CREATE_OK);
    ASSERT_INT_EQ(0, job_system_start(&sys));

    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = default_rt_config();
    cfg.vm_config.fuel_budget = 1000;
    ASSERT_TRUE(aegis_script_runtime_init(&rt, &cfg));

    /* Script: read event source into r0, add to r10 (accumulator),
     * then yield to wait for next event. After 3 events, r10 should
     * be the sum of all sources. Script loops forever via yield. */
    const char *src =
        ".topic !add\n"
        "mov r10, 0\n"        /* accumulator */
        "top:\n"
        "event_src r0\n"      /* read source from current event */
        "add r10, r10, r0\n"  /* accumulate */
        "yield\n"             /* wait for next event */
        "jmp top\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    uint32_t id = aegis_script_runtime_load(&rt, "accumulator", &bc);
    ASSERT_TRUE(id != AEGIS_SCRIPT_ID_INVALID);

    aegis_script_runtime_start(&rt, id, &sys);

    /* Push 3 events with sources 10, 20, 30. */
    uint32_t add_hash = aegis_topic_hash("!add");
    for (uint32_t i = 0; i < 3; i++) {
        aegis_event_t ev = {0};
        ev.type = add_hash;
        ev.source = (i + 1) * 10;
        ev.tick = i + 1;
        aegis_script_runtime_publish(&rt, &ev);
    }

    /* The fiber processes events and keeps yielding.
     * We can't use wait_idle since the fiber never finishes.
     * Instead, busy-wait until r10 reaches 60 (with timeout).
     * Use volatile read to prevent compiler from caching the value. */
    volatile uint32_t *r10_ptr = &rt.instances[id].vm.regs[10].u32;
    for (int attempt = 0; attempt < 1000000; attempt++) {
        if (*r10_ptr == 60) break;
        for (volatile int spin = 0; spin < 100; spin++) {}
    }

    /* r10 should be 10 + 20 + 30 = 60. */
    ASSERT_UINT_EQ(60, rt.instances[id].vm.regs[10].u32);
    /* Script should still be alive (yielded, not exited). */
    ASSERT_TRUE(rt.instances[id].active);

    /* Deactivate so the fiber exits on next yield check. */
    rt.instances[id].active = false;

    free(bc.instructions);
    aegis_script_runtime_destroy(&rt);
    job_system_shutdown(&sys);
    return 0;
}

/* ======================================================================= */
/* Test: error in script marks inactive                                     */
/* ======================================================================= */

static int test_script_error_marks_inactive(void) {
    job_system_t sys = {0};
    job_system_create_status_t jstatus =
        job_system_create(&sys, 2, 64, 64 * 1024, 256, 0);
    ASSERT_TRUE(jstatus == JOB_CREATE_OK);
    ASSERT_INT_EQ(0, job_system_start(&sys));

    aegis_script_runtime_t rt;
    aegis_runtime_config_t cfg = default_rt_config();
    cfg.vm_config.fuel_budget = 1000;
    ASSERT_TRUE(aegis_script_runtime_init(&rt, &cfg));

    /* Script that triggers a runtime error: read event field with
     * payload_len=0, which returns error 0xFFDF. Actually let's
     * just try to access event with no event set... We'll do
     * something that causes an error: stack underflow via pop. */
    const char *src =
        ".topic !err\n"
        "pop r0\n"            /* stack underflow -> error */
        "exit 0\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    uint32_t id = aegis_script_runtime_load(&rt, "bad_script", &bc);
    ASSERT_TRUE(id != AEGIS_SCRIPT_ID_INVALID);

    aegis_script_runtime_start(&rt, id, &sys);

    aegis_event_t ev = {0};
    ev.type = aegis_topic_hash("!err");
    aegis_script_runtime_publish(&rt, &ev);

    job_system_wait_idle(&sys);

    /* Script should be marked inactive due to error. */
    ASSERT_TRUE(!rt.instances[id].active);
    ASSERT_UINT_EQ(AEGIS_VM_ERROR, rt.instances[id].vm.status);

    free(bc.instructions);
    aegis_script_runtime_destroy(&rt);
    job_system_shutdown(&sys);
    return 0;
}

/* ======================================================================= */
/* Main                                                                     */
/* ======================================================================= */

int main(void) {
    printf("=== Aegis Script Runtime Tests ===\n\n");

    RUN(test_init_destroy);
    RUN(test_load_script);
    RUN(test_topic_auto_subscribe);
    RUN(test_load_at_capacity);
    RUN(test_fiber_execution_exit);
    RUN(test_fiber_force_yield_resume);
    RUN(test_multiple_scripts_concurrent);
    RUN(test_event_routing_selective);
    RUN(test_yield_processes_multiple_events);
    RUN(test_script_error_marks_inactive);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
