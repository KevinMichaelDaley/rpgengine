/**
 * @file aegis_ops_event_tests.c
 * @brief Tests for Aegis event access instructions: event_type, event_src,
 *        event_field.
 *
 * Tests the instruction handlers directly (unit tests) and through the VM
 * interpreter (integration tests).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/aegis/aegis_event.h"
#include "ferrum/aegis/aegis_ops_event.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_bytecode.h"
#include "ferrum/aegis/aegis_types.h"

/* ======================================================================= */
/* Test harness                                                             */
/* ======================================================================= */

static int g_pass;
static int g_fail;

#define ASSERT(cond)                                                         \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__);      \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define ASSERT_INT_EQ(expected, actual)                                      \
    do {                                                                     \
        int _e = (expected), _a = (actual);                                  \
        if (_e != _a) {                                                      \
            printf("  ASSERT FAILED: %d != %d (line %d)\n", _e, _a,         \
                   __LINE__);                                                \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define ASSERT_U32_EQ(expected, actual)                                      \
    do {                                                                     \
        uint32_t _e = (expected), _a = (actual);                             \
        if (_e != _a) {                                                      \
            printf("  ASSERT FAILED: %u != %u (line %d)\n", _e, _a,         \
                   __LINE__);                                                \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define ASSERT_FLOAT_EQ(expected, actual)                                    \
    do {                                                                     \
        float _e = (expected), _a = (actual);                                \
        float _d = _e - _a;                                                  \
        if (_d < -0.0001f || _d > 0.0001f) {                                 \
            printf("  ASSERT FAILED: %f != %f (line %d)\n",                  \
                   (double)_e, (double)_a, __LINE__);                        \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define RUN(fn)                                                              \
    do {                                                                     \
        printf("RUN  " #fn "\n");                                            \
        if (fn()) { g_pass++; printf("OK   " #fn "\n"); }                    \
        else      { g_fail++; printf("FAIL " #fn "\n"); }                    \
    } while (0)

/* ======================================================================= */
/* Helpers for building test programs                                       */
/* ======================================================================= */

#define DEFAULT_PROG_CAP 256
#define DEFAULT_ARENA    4096

typedef struct {
    aegis_instruction_t *insns;
    uint32_t             count;
    uint32_t             cap;
    uint8_t             *arena;
    uint32_t             arena_size;
    aegis_bytecode_t     bc;
    aegis_vm_t           vm;
} test_program_t;

static void prog_alloc(test_program_t *p, uint32_t cap, uint32_t arena_size) {
    p->insns      = (aegis_instruction_t *)malloc(cap * sizeof(aegis_instruction_t));
    p->count      = 0;
    p->cap        = cap;
    p->arena      = (uint8_t *)malloc(arena_size);
    p->arena_size = arena_size;
}

static void prog_free(test_program_t *p) {
    free(p->insns);
    free(p->arena);
    p->insns = NULL;
    p->arena = NULL;
}

static void emit(test_program_t *p, aegis_instruction_t insn) {
    if (p->count < p->cap) {
        p->insns[p->count++] = insn;
    }
}

/** Build instruction: all register operands. */
static aegis_instruction_t insn_rrr(aegis_opcode_t op,
                                    uint32_t a, uint32_t b, uint32_t c) {
    return aegis_insn_make(op, 0, a, b, c);
}

/** Build instruction: A=reg, B=imm, C=imm. */
static aegis_instruction_t insn_rii(aegis_opcode_t op,
                                    uint32_t a, uint32_t b, uint32_t c) {
    return aegis_insn_make(op, AEGIS_IMM_B | AEGIS_IMM_C, a, b, c);
}

static bool prog_build(test_program_t *p, uint32_t fuel) {
    memset(p->arena, 0, p->arena_size);
    p->bc.instructions      = p->insns;
    p->bc.instruction_count = p->count;
    aegis_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.fuel_budget = fuel;
    memset(&p->vm, 0, sizeof(p->vm));
    return aegis_vm_init(&p->vm, &p->bc, &cfg, p->arena, p->arena_size);
}

/* ======================================================================= */
/* Unit Tests: Direct handler calls                                         */
/* ======================================================================= */

/* ----------------------------------------------------------------------- */
/* Test: event_type loads type hash                                         */
/* ----------------------------------------------------------------------- */

static bool test_unit_event_type(void) {
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = 0x12345678;

    aegis_register_t dst;
    ASSERT(aegis_op_event_type(&dst, &ev));
    ASSERT_U32_EQ(0x12345678, dst.u32);

    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_type with NULL event returns false                           */
/* ----------------------------------------------------------------------- */

static bool test_unit_event_type_null(void) {
    aegis_register_t dst;
    ASSERT(!aegis_op_event_type(&dst, NULL));
    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_src loads source entity ID                                   */
/* ----------------------------------------------------------------------- */

static bool test_unit_event_src(void) {
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.source = 42;

    aegis_register_t dst;
    ASSERT(aegis_op_event_src(&dst, &ev));
    ASSERT_U32_EQ(42, dst.u32);

    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_src with NULL event returns false                            */
/* ----------------------------------------------------------------------- */

static bool test_unit_event_src_null(void) {
    aegis_register_t dst;
    ASSERT(!aegis_op_event_src(&dst, NULL));
    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_field reads a float from payload                             */
/* ----------------------------------------------------------------------- */

static bool test_unit_event_field_f32(void) {
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    float val = 3.14f;
    memcpy(ev.payload, &val, sizeof(val));
    ev.payload_len = sizeof(val);

    aegis_register_t dst;
    ASSERT(aegis_op_event_field(&dst, &ev, 0, 4));
    ASSERT_FLOAT_EQ(3.14f, dst.f32);

    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_field reads vec3 (12 bytes) from payload                     */
/* ----------------------------------------------------------------------- */

static bool test_unit_event_field_vec3(void) {
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    float vec[3] = {1.0f, 2.0f, 3.0f};
    memcpy(ev.payload, vec, 12);
    ev.payload_len = 12;

    aegis_register_t dst;
    ASSERT(aegis_op_event_field(&dst, &ev, 0, 12));
    ASSERT_FLOAT_EQ(1.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(2.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(3.0f, dst.vec3[2]);

    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_field at offset within payload                               */
/* ----------------------------------------------------------------------- */

static bool test_unit_event_field_offset(void) {
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    /* Two i32 values: [100, 200] */
    int32_t vals[2] = {100, 200};
    memcpy(ev.payload, vals, 8);
    ev.payload_len = 8;

    aegis_register_t dst;
    /* Read second i32 at offset 4. */
    ASSERT(aegis_op_event_field(&dst, &ev, 4, 4));
    ASSERT_INT_EQ(200, dst.i32);

    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_field out of bounds → returns false                          */
/* ----------------------------------------------------------------------- */

static bool test_unit_event_field_oob(void) {
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.payload_len = 4;

    aegis_register_t dst;
    /* offset(4) + size(4) = 8 > payload_len(4) → OOB. */
    ASSERT(!aegis_op_event_field(&dst, &ev, 4, 4));
    /* offset(0) + size(8) = 8 > payload_len(4) → OOB. */
    ASSERT(!aegis_op_event_field(&dst, &ev, 0, 8));
    /* size > 16 → always rejected. */
    ASSERT(!aegis_op_event_field(&dst, &ev, 0, 17));

    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_field with NULL event → false                                */
/* ----------------------------------------------------------------------- */

static bool test_unit_event_field_null(void) {
    aegis_register_t dst;
    ASSERT(!aegis_op_event_field(&dst, NULL, 0, 4));
    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_field zero-length read                                       */
/* ----------------------------------------------------------------------- */

static bool test_unit_event_field_zero_size(void) {
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.payload_len = 4;

    aegis_register_t dst;
    /* Zero-size read should succeed (no bytes copied, dst zeroed). */
    ASSERT(aegis_op_event_field(&dst, &ev, 0, 0));
    /* All bytes should be zero. */
    for (int i = 0; i < 16; i++) {
        ASSERT_INT_EQ(0, dst.bytes[i]);
    }

    return true;
}

/* ======================================================================= */
/* Integration Tests: Through the VM interpreter                            */
/* ======================================================================= */

/* ----------------------------------------------------------------------- */
/* Test: event_type instruction via VM                                      */
/* ----------------------------------------------------------------------- */

static bool test_vm_event_type(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_ARENA);

    /* Program: event_type r0; yield */
    emit(&p, insn_rrr(AEGIS_OP_EVENT_TYPE, 0, 0, 0));
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 100));

    /* Attach an event. */
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type   = 0xABCD1234;
    ev.source = 99;
    p.vm.event = &ev;

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_U32_EQ(0xABCD1234, p.vm.regs[0].u32);

    prog_free(&p);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_src instruction via VM                                       */
/* ----------------------------------------------------------------------- */

static bool test_vm_event_src(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_ARENA);

    /* Program: event_src r0; yield */
    emit(&p, insn_rrr(AEGIS_OP_EVENT_SRC, 0, 0, 0));
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 100));

    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type   = 0x1111;
    ev.source = 777;
    p.vm.event = &ev;

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_U32_EQ(777, p.vm.regs[0].u32);

    prog_free(&p);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_field instruction via VM (f32)                               */
/* ----------------------------------------------------------------------- */

static bool test_vm_event_field_f32(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_ARENA);

    /* Program: event_field r0, offset=0, size=4; yield */
    emit(&p, insn_rii(AEGIS_OP_EVENT_FIELD, 0, 0, 4));
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 100));

    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = 0x1111;
    float val = 42.5f;
    memcpy(ev.payload, &val, sizeof(val));
    ev.payload_len = sizeof(val);
    p.vm.event = &ev;

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_FLOAT_EQ(42.5f, p.vm.regs[0].f32);

    prog_free(&p);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_field OOB via VM → error                                     */
/* ----------------------------------------------------------------------- */

static bool test_vm_event_field_oob(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_ARENA);

    /* Program: event_field r0, offset=8, size=4 (payload only has 4 bytes); yield */
    emit(&p, insn_rii(AEGIS_OP_EVENT_FIELD, 0, 8, 4));
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 100));

    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = 0x1111;
    ev.payload_len = 4;
    p.vm.event = &ev;

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);

    prog_free(&p);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: event_type with no event attached → error                          */
/* ----------------------------------------------------------------------- */

static bool test_vm_event_no_event(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_ARENA);

    /* Program: event_type r0; yield */
    emit(&p, insn_rrr(AEGIS_OP_EVENT_TYPE, 0, 0, 0));
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 100));
    p.vm.event = NULL; /* No event. */

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);

    prog_free(&p);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: Full event processing pipeline                                     */
/* Read type, source, and a vec3 payload field, then yield.                 */
/* ----------------------------------------------------------------------- */

static bool test_vm_full_event_processing(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_ARENA);

    /* Program:
     *   event_type  r0           → type hash
     *   event_src   r1           → source entity ID
     *   event_field r2, 0, 12    → vec3 from payload
     *   event_field r3, 12, 4    → f32 from payload at offset 12
     *   yield
     */
    emit(&p, insn_rrr(AEGIS_OP_EVENT_TYPE, 0, 0, 0));
    emit(&p, insn_rrr(AEGIS_OP_EVENT_SRC, 1, 0, 0));
    emit(&p, insn_rii(AEGIS_OP_EVENT_FIELD, 2, 0, 12));
    emit(&p, insn_rii(AEGIS_OP_EVENT_FIELD, 3, 12, 4));
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 100));

    /* Build event with vec3 + f32 payload. */
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type   = aegis_topic_hash("!hit");
    ev.source = 42;
    ev.tick   = 100;
    float pos[3] = {10.0f, 20.0f, 30.0f};
    float dmg = 25.0f;
    memcpy(ev.payload, pos, 12);
    memcpy(ev.payload + 12, &dmg, 4);
    ev.payload_len = 16;
    p.vm.event = &ev;

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);

    ASSERT_U32_EQ(ev.type, p.vm.regs[0].u32);
    ASSERT_U32_EQ(42, p.vm.regs[1].u32);
    ASSERT_FLOAT_EQ(10.0f, p.vm.regs[2].vec3[0]);
    ASSERT_FLOAT_EQ(20.0f, p.vm.regs[2].vec3[1]);
    ASSERT_FLOAT_EQ(30.0f, p.vm.regs[2].vec3[2]);
    ASSERT_FLOAT_EQ(25.0f, p.vm.regs[3].f32);

    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Main                                                                     */
/* ======================================================================= */

int main(void) {
    printf("=== Aegis Event Access Instruction Tests ===\n\n");

    /* Unit tests (direct handler calls) */
    RUN(test_unit_event_type);
    RUN(test_unit_event_type_null);
    RUN(test_unit_event_src);
    RUN(test_unit_event_src_null);
    RUN(test_unit_event_field_f32);
    RUN(test_unit_event_field_vec3);
    RUN(test_unit_event_field_offset);
    RUN(test_unit_event_field_oob);
    RUN(test_unit_event_field_null);
    RUN(test_unit_event_field_zero_size);

    /* Integration tests (through VM) */
    RUN(test_vm_event_type);
    RUN(test_vm_event_src);
    RUN(test_vm_event_field_f32);
    RUN(test_vm_event_field_oob);
    RUN(test_vm_event_no_event);
    RUN(test_vm_full_event_processing);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
