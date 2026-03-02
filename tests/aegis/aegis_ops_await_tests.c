/**
 * @file aegis_ops_await_tests.c
 * @brief Tests for AWAIT_EVENT instruction handler.
 *
 * Covers: matching event found, no match (wait-yield), multiple topics,
 * event data packing, queue empty, and VM integration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/aegis/aegis_ops_signal.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/aegis/aegis_decode.h"
#include "ferrum/aegis/aegis_asm.h"

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

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void init_test_vm(aegis_vm_t *vm, aegis_bytecode_t *bc,
                         uint8_t *arena, uint32_t arena_sz,
                         aegis_topic_table_t *topics,
                         aegis_event_queue_t *eq) {
    memset(vm, 0, sizeof(*vm));
    aegis_config_t cfg = aegis_config_default();
    cfg.arena_size = arena_sz;
    cfg.static_max = 256;
    cfg.stack_max = 256;
    aegis_vm_init(vm, bc, &cfg, arena, arena_sz);
    vm->topic_table = topics;
    vm->event_queue = eq;
    vm->script_id = 0;
    vm->signal_rate_limit_us = 250;
}

/* ------------------------------------------------------------------ */
/* Await event tests                                                  */
/* ------------------------------------------------------------------ */

static void test_await_event_match(void) {
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    /* Push a matching event. */
    aegis_event_t ev = {0};
    ev.type = 42;
    ev.source = 7;
    ev.payload[0] = 0xAA;
    ev.payload_len = 8;
    aegis_event_queue_push(&eq, &ev);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), NULL, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_AWAIT_EVENT;
    d.raw_a = 0; /* dst */
    d.raw_b = 1; /* topic hash reg */
    vm.regs[1].u32 = 42;

    bool ok = aegis_op_await_event(&vm, &d);
    ASSERT_TRUE(ok); /* event found, advance PC */

    /* Check packed result: type(u32) + source(u32) + first 8 payload bytes. */
    ASSERT_EQ(vm.regs[0].u32, 42);

    /* Queue should now be empty (event was consumed). */
    ASSERT_EQ(aegis_event_queue_count(&eq), 0);

    aegis_event_queue_destroy(&eq);
}

static void test_await_event_no_match(void) {
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    /* Push event with different topic. */
    aegis_event_t ev = {0};
    ev.type = 99;
    aegis_event_queue_push(&eq, &ev);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), NULL, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_AWAIT_EVENT;
    d.raw_a = 0;
    d.raw_b = 1;
    vm.regs[1].u32 = 42; /* Looking for 42, queue has 99. */

    bool ok = aegis_op_await_event(&vm, &d);
    ASSERT_FALSE(ok); /* No match → wait-yield. */

    /* Event should still be in queue (not consumed). */
    ASSERT_EQ(aegis_event_queue_count(&eq), 1);

    aegis_event_queue_destroy(&eq);
}

static void test_await_event_empty_queue(void) {
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), NULL, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_AWAIT_EVENT;
    d.raw_a = 0;
    d.raw_b = 1;
    vm.regs[1].u32 = 42;

    bool ok = aegis_op_await_event(&vm, &d);
    ASSERT_FALSE(ok); /* Empty queue → wait-yield. */

    aegis_event_queue_destroy(&eq);
}

static void test_await_event_skips_nonmatching(void) {
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    /* Push non-matching first, then matching. */
    aegis_event_t ev1 = {0};
    ev1.type = 10;
    aegis_event_queue_push(&eq, &ev1);

    aegis_event_t ev2 = {0};
    ev2.type = 42;
    ev2.source = 5;
    aegis_event_queue_push(&eq, &ev2);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), NULL, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_AWAIT_EVENT;
    d.raw_a = 0;
    d.raw_b = 1;
    vm.regs[1].u32 = 42;

    bool ok = aegis_op_await_event(&vm, &d);
    ASSERT_TRUE(ok); /* Found matching event. */
    ASSERT_EQ(vm.regs[0].u32, 42);

    /* Non-matching event should still be in queue. */
    ASSERT_EQ(aegis_event_queue_count(&eq), 1);

    aegis_event_queue_destroy(&eq);
}

static void test_await_event_no_queue(void) {
    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), NULL, NULL);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_AWAIT_EVENT;
    d.raw_a = 0;
    d.raw_b = 1;
    vm.regs[1].u32 = 42;

    bool ok = aegis_op_await_event(&vm, &d);
    ASSERT_FALSE(ok); /* No queue → wait-yield. */
}

static void test_await_event_packs_source(void) {
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    aegis_event_t ev = {0};
    ev.type = 42;
    ev.source = 0xBEEF;
    ev.payload[0] = 0x11;
    ev.payload[1] = 0x22;
    ev.payload_len = 8;
    aegis_event_queue_push(&eq, &ev);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), NULL, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_AWAIT_EVENT;
    d.raw_a = 3; /* dst = r3 */
    d.raw_b = 1;
    vm.regs[1].u32 = 42;

    bool ok = aegis_op_await_event(&vm, &d);
    ASSERT_TRUE(ok);

    /* First 4 bytes = type, next 4 = source. */
    uint32_t packed_type, packed_source;
    memcpy(&packed_type, vm.regs[3].bytes, 4);
    memcpy(&packed_source, vm.regs[3].bytes + 4, 4);
    ASSERT_EQ(packed_type, 42);
    ASSERT_EQ(packed_source, 0xBEEF);

    aegis_event_queue_destroy(&eq);
}

/* ------------------------------------------------------------------ */
/* VM integration                                                     */
/* ------------------------------------------------------------------ */

static void test_vm_await_event_wait_yield(void) {
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    /* Empty queue — await should cause wait-yield. */
    const char *src =
        "load_imm r1 42\n"
        "await_event r0 r1\n"
        "exit r0\n";

    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, src, (uint32_t)strlen(src), &bc));

    uint8_t arena[4096];
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), NULL, &eq);

    aegis_vm_status_t status = aegis_vm_run(&vm);
    ASSERT_EQ(status, AEGIS_VM_WAIT_YIELDED);
    /* PC should NOT have advanced past await_event. */
    ASSERT_EQ(vm.pc, 1);

    /* Now push matching event and resume. */
    aegis_event_t ev = {0};
    ev.type = 42;
    ev.source = 3;
    aegis_event_queue_push(&eq, &ev);

    aegis_vm_reset_fuel(&vm);
    status = aegis_vm_run(&vm);
    ASSERT_EQ(status, AEGIS_VM_EXITED);
    /* r0 should have event type packed. */
    ASSERT_EQ(vm.regs[0].u32, 42);

    free(bc.instructions);
    aegis_event_queue_destroy(&eq);
}

static void test_vm_await_event_immediate(void) {
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    /* Pre-fill queue with matching event. */
    aegis_event_t ev = {0};
    ev.type = 77;
    ev.source = 12;
    aegis_event_queue_push(&eq, &ev);

    const char *src =
        "load_imm r1 77\n"
        "await_event r0 r1\n"
        "exit r0\n";

    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, src, (uint32_t)strlen(src), &bc));

    uint8_t arena[4096];
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), NULL, &eq);

    aegis_vm_status_t status = aegis_vm_run(&vm);
    ASSERT_EQ(status, AEGIS_VM_EXITED);
    ASSERT_EQ(vm.regs[0].u32, 77);

    free(bc.instructions);
    aegis_event_queue_destroy(&eq);
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== aegis_ops_await_tests ===\n");

    RUN(test_await_event_match);
    RUN(test_await_event_no_match);
    RUN(test_await_event_empty_queue);
    RUN(test_await_event_skips_nonmatching);
    RUN(test_await_event_no_queue);
    RUN(test_await_event_packs_source);
    RUN(test_vm_await_event_wait_yield);
    RUN(test_vm_await_event_immediate);

    printf("\nRESULTS: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
