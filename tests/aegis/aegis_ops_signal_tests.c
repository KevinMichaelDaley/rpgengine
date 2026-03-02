/**
 * @file aegis_ops_signal_tests.c
 * @brief Tests for SIGNAL and SUBSCRIBE instruction handlers.
 *
 * Covers: signal success, rate-limited, invalid topic (hash=0),
 * subscribe success, already subscribed, table full, and VM
 * integration via IL assembly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    vm->last_signal_time_us = 0;
}

/* ------------------------------------------------------------------ */
/* Signal tests                                                       */
/* ------------------------------------------------------------------ */

static void test_signal_success(void) {
    aegis_topic_table_t topics;
    aegis_topic_table_init(&topics, 64, 16);

    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    /* Subscribe script 0 to topic 42 so we can verify delivery. */
    aegis_topic_subscribe(&topics, 42, 0);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), &topics, &eq);

    /* Set up decoded instruction: a=dst(r0), b=topic_hash(r1), c=payload(r2). */
    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_SIGNAL;
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;
    vm.regs[1].u32 = 42; /* topic hash */
    vm.regs[2].u32 = 0xDEADBEEF; /* payload */

    bool ok = aegis_op_signal(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_EQ(vm.regs[0].i32, 0); /* success */

    /* Verify event was delivered to our queue. */
    aegis_event_t ev;
    ASSERT_TRUE(aegis_event_queue_pop(&eq, &ev));
    ASSERT_EQ(ev.type, 42);

    aegis_topic_table_destroy(&topics);
    aegis_event_queue_destroy(&eq);
}

static void test_signal_rate_limited(void) {
    aegis_topic_table_t topics;
    aegis_topic_table_init(&topics, 64, 16);

    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), &topics, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_SIGNAL;
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;
    vm.regs[1].u32 = 42;

    /* First signal should succeed. */
    aegis_op_signal(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 0);

    /* Immediate second signal should be rate-limited (< 250us). */
    aegis_op_signal(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 1); /* rate-limited */

    /* Wait 400us, then signal again — should succeed. */
    struct timespec ts = {0, 400000}; /* 400 microseconds */
    nanosleep(&ts, NULL);
    aegis_op_signal(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 0); /* success */

    aegis_topic_table_destroy(&topics);
    aegis_event_queue_destroy(&eq);
}

static void test_signal_invalid_topic(void) {
    aegis_topic_table_t topics;
    aegis_topic_table_init(&topics, 64, 16);

    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), &topics, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_SIGNAL;
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;
    vm.regs[1].u32 = 0; /* hash 0 = invalid */

    aegis_op_signal(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 2); /* invalid topic */

    aegis_topic_table_destroy(&topics);
    aegis_event_queue_destroy(&eq);
}

static void test_signal_no_topic_table(void) {
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), NULL, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_SIGNAL;
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;
    vm.regs[1].u32 = 42;

    aegis_op_signal(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 2); /* no topic table = invalid */

    aegis_event_queue_destroy(&eq);
}

static void test_signal_payload_packed(void) {
    aegis_topic_table_t topics;
    aegis_topic_table_init(&topics, 64, 16);

    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);
    aegis_topic_subscribe(&topics, 99, 0);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), &topics, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_SIGNAL;
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;
    vm.regs[1].u32 = 99;
    /* Write known pattern into payload register. */
    memset(vm.regs[2].bytes, 0xAB, 16);

    aegis_op_signal(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 0);

    aegis_event_t ev;
    ASSERT_TRUE(aegis_event_queue_pop(&eq, &ev));
    ASSERT_EQ(ev.type, 99);
    ASSERT_EQ(ev.payload_len, 16);
    ASSERT_EQ(ev.payload[0], 0xAB);
    ASSERT_EQ(ev.payload[15], 0xAB);

    aegis_topic_table_destroy(&topics);
    aegis_event_queue_destroy(&eq);
}

/* ------------------------------------------------------------------ */
/* Subscribe tests                                                    */
/* ------------------------------------------------------------------ */

static void test_subscribe_success(void) {
    aegis_topic_table_t topics;
    aegis_topic_table_init(&topics, 64, 16);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);
    init_test_vm(&vm, &bc, arena, sizeof(arena), &topics, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_SUBSCRIBE;
    d.raw_a = 0; /* dst */
    d.raw_b = 1; /* topic hash reg */
    vm.regs[1].u32 = 77;

    aegis_op_subscribe(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 0); /* success */

    /* Verify subscription exists by publishing. */
    aegis_event_t ev = {0};
    ev.type = 77;
    aegis_topic_publish(&topics, &ev, &eq, 1);
    ASSERT_EQ(aegis_event_queue_count(&eq), 1);

    aegis_topic_table_destroy(&topics);
    aegis_event_queue_destroy(&eq);
}

static void test_subscribe_duplicate(void) {
    aegis_topic_table_t topics;
    aegis_topic_table_init(&topics, 64, 16);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);
    init_test_vm(&vm, &bc, arena, sizeof(arena), &topics, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_SUBSCRIBE;
    d.raw_a = 0;
    d.raw_b = 1;
    vm.regs[1].u32 = 77;

    aegis_op_subscribe(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 0); /* success */

    aegis_op_subscribe(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 1); /* already subscribed */

    aegis_topic_table_destroy(&topics);
    aegis_event_queue_destroy(&eq);
}

static void test_subscribe_table_full(void) {
    /* Create table with capacity 1. */
    aegis_topic_table_t topics;
    aegis_topic_table_init(&topics, 1, 16);

    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);
    init_test_vm(&vm, &bc, arena, sizeof(arena), &topics, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_SUBSCRIBE;
    d.raw_a = 0;
    d.raw_b = 1;

    /* First subscribe fills the table. */
    vm.regs[1].u32 = 10;
    aegis_op_subscribe(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 0);

    /* Second subscribe overflows. */
    vm.regs[1].u32 = 20;
    aegis_op_subscribe(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 2); /* table full */

    aegis_topic_table_destroy(&topics);
    aegis_event_queue_destroy(&eq);
}

static void test_subscribe_no_topic_table(void) {
    uint8_t arena[4096];
    aegis_bytecode_t bc = {0};
    aegis_vm_t vm;
    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);
    init_test_vm(&vm, &bc, arena, sizeof(arena), NULL, &eq);

    aegis_decode_result_t d;
    memset(&d, 0, sizeof(d));
    d.opcode = AEGIS_OP_SUBSCRIBE;
    d.raw_a = 0;
    d.raw_b = 1;
    vm.regs[1].u32 = 42;

    aegis_op_subscribe(&vm, &d);
    ASSERT_EQ(vm.regs[0].i32, 2); /* no table = full */

    aegis_event_queue_destroy(&eq);
}

/* ------------------------------------------------------------------ */
/* VM integration: assemble and run SIGNAL + SUBSCRIBE                */
/* ------------------------------------------------------------------ */

static void test_vm_signal_integration(void) {
    aegis_topic_table_t topics;
    aegis_topic_table_init(&topics, 64, 16);

    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);
    aegis_topic_subscribe(&topics, 55, 0);

    const char *src =
        "load_imm r1 55\n"    /* topic hash */
        "load_imm r2 0\n"     /* payload */
        "signal r0 r1 r2\n"   /* signal → r0 = status */
        "exit r0\n";

    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, src, (uint32_t)strlen(src), &bc));

    uint8_t arena[4096];
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), &topics, &eq);

    aegis_vm_status_t status = aegis_vm_run(&vm);
    ASSERT_EQ(status, AEGIS_VM_EXITED);
    ASSERT_EQ(vm.exit_code, 0); /* signal returned success */

    /* Verify event was delivered. */
    aegis_event_t ev;
    ASSERT_TRUE(aegis_event_queue_pop(&eq, &ev));
    ASSERT_EQ(ev.type, 55);

    free(bc.instructions);
    aegis_topic_table_destroy(&topics);
    aegis_event_queue_destroy(&eq);
}

static void test_vm_subscribe_integration(void) {
    aegis_topic_table_t topics;
    aegis_topic_table_init(&topics, 64, 16);

    aegis_event_queue_t eq;
    aegis_event_queue_init(&eq, 16);

    const char *src =
        "load_imm r1 88\n"       /* topic hash */
        "subscribe r0 r1\n"      /* subscribe → r0 = status */
        "exit r0\n";

    aegis_asm_t as;
    aegis_asm_init(&as);
    aegis_bytecode_t bc;
    ASSERT_TRUE(aegis_asm_compile(&as, src, (uint32_t)strlen(src), &bc));

    uint8_t arena[4096];
    aegis_vm_t vm;
    init_test_vm(&vm, &bc, arena, sizeof(arena), &topics, &eq);

    aegis_vm_status_t status = aegis_vm_run(&vm);
    ASSERT_EQ(status, AEGIS_VM_EXITED);
    ASSERT_EQ(vm.exit_code, 0); /* subscribe success */

    /* Verify subscription works: publish and check queue. */
    aegis_event_t ev = {0};
    ev.type = 88;
    aegis_topic_publish(&topics, &ev, &eq, 1);
    ASSERT_EQ(aegis_event_queue_count(&eq), 1);

    free(bc.instructions);
    aegis_topic_table_destroy(&topics);
    aegis_event_queue_destroy(&eq);
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== aegis_ops_signal_tests ===\n");

    RUN(test_signal_success);
    RUN(test_signal_rate_limited);
    RUN(test_signal_invalid_topic);
    RUN(test_signal_no_topic_table);
    RUN(test_signal_payload_packed);

    RUN(test_subscribe_success);
    RUN(test_subscribe_duplicate);
    RUN(test_subscribe_table_full);
    RUN(test_subscribe_no_topic_table);

    RUN(test_vm_signal_integration);
    RUN(test_vm_subscribe_integration);

    printf("\nRESULTS: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
