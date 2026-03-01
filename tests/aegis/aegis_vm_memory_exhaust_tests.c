/**
 * @file aegis_vm_memory_exhaust_tests.c
 * @brief Tests for Aegis VM memory exhaustion handling.
 *
 * Verifies the VM correctly returns errors when scripts try to:
 * - Allocate more heap than available
 * - Overflow the call stack with deep recursion
 * - Push more data than the stack can hold
 * - Write out-of-bounds on the heap
 * - Write out-of-bounds on static storage
 * - Repeatedly allocate until exhaustion in a loop
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_types.h"

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

#define ASSERT_INT_EQ(a, b) do { \
    if ((int)(a) != (int)(b)) { \
        printf("  ASSERT FAILED: %d != %d (line %d)\n", \
               (int)(a), (int)(b), __LINE__); \
        return false; \
    } \
} while (0)

/* ----------------------------------------------------------------------- */
/* Instruction builders                                                    */
/* ----------------------------------------------------------------------- */

static aegis_instruction_t make_insn(aegis_opcode_t op, uint16_t flags,
                                     uint32_t a, uint32_t b, uint32_t c) {
    aegis_instruction_t insn;
    insn.words[0] = (uint32_t)op | ((uint32_t)flags << 16);
    insn.words[1] = a;
    insn.words[2] = b;
    insn.words[3] = c;
    return insn;
}

static aegis_instruction_t insn_rrr(aegis_opcode_t op,
                                    uint32_t a, uint32_t b, uint32_t c) {
    return make_insn(op, 0, a, b, c);
}

static aegis_instruction_t insn_rir(aegis_opcode_t op,
                                    uint32_t a, uint32_t imm_b, uint32_t c) {
    return make_insn(op, 0x02, a, imm_b, c);
}

static aegis_instruction_t insn_irr(aegis_opcode_t op,
                                    uint32_t imm_a, uint32_t b, uint32_t c) {
    return make_insn(op, 0x01, imm_a, b, c);
}

/* ----------------------------------------------------------------------- */
/* Dynamic program infrastructure                                         */
/* ----------------------------------------------------------------------- */

#define DEFAULT_PROG_CAP  (1u << 16)

typedef struct test_program {
    aegis_instruction_t *insns;
    uint32_t             count;
    uint32_t             capacity;
    aegis_bytecode_t     bc;
    aegis_vm_t           vm;
    uint8_t             *arena;
    uint32_t             arena_size;
} test_program_t;

static void prog_alloc(test_program_t *p, uint32_t insn_cap,
                       uint32_t vm_arena) {
    memset(p, 0, sizeof(*p));
    p->capacity   = insn_cap;
    p->arena_size = vm_arena;
    p->insns = (aegis_instruction_t *)calloc(insn_cap,
                                              sizeof(aegis_instruction_t));
    p->arena = (uint8_t *)calloc(vm_arena, 1);
}

static void prog_free(test_program_t *p) {
    free(p->insns);
    free(p->arena);
    memset(p, 0, sizeof(*p));
}

static bool prog_build_ex(test_program_t *p, uint32_t fuel,
                           uint32_t static_size, uint32_t stack_max) {
    memset(p->arena, 0, p->arena_size);
    memset(&p->vm, 0, sizeof(p->vm));
    aegis_bytecode_init(&p->bc);
    p->bc.instructions      = p->insns;
    p->bc.instruction_count = p->count;
    p->bc.static_size       = static_size;

    aegis_config_t cfg = aegis_config_default();
    cfg.fuel_budget = fuel;
    cfg.stack_max   = stack_max;

    return aegis_vm_init(&p->vm, &p->bc, &cfg, p->arena, p->arena_size);
}

static void emit(test_program_t *p, aegis_instruction_t insn) {
    if (p->count < p->capacity) {
        p->insns[p->count++] = insn;
    }
}

/* ======================================================================= */
/* Test: Single alloc larger than entire heap → error                      */
/* ======================================================================= */

static bool test_alloc_too_large(void) {
    /* Small arena: 2048 bytes. Static=64, stack=256.
     * Heap = 2048 - 64 - 256 = 1728 bytes.
     * Requesting 4096 should fail. */
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, 2048);

    emit(&p, insn_rir(AEGIS_OP_ALLOC, 0, 4096, 0));  /* alloc 4096 bytes */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build_ex(&p, 1000, 64, 256));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Repeated alloc in a loop until heap is exhausted                  */
/* ======================================================================= */

static bool test_alloc_loop_exhaustion(void) {
    /* Arena: 4096. Static=64, stack=256. Heap=3776.
     * Each alloc = 128 bytes → 29 should succeed, 30th fails.
     * Loop: alloc 128 each iteration, count successes. */
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, 4096);

    /*
     * r0 = alloc result (ignored, just testing for error)
     * r1 = count (0)
     * r2 = one (1)
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 0, 0));     /* 0: count=0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 1, 0));     /* 1: one=1 */

    /* loop (PC=2): */
    emit(&p, insn_rir(AEGIS_OP_ALLOC, 0, 128, 0));       /* 2: alloc 128 */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 1, 1, 2));           /* 3: count++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 2, 0, 0));           /* 4: goto loop */

    ASSERT(prog_build_ex(&p, 1000000, 64, 256));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    /* Should error when alloc fails. */
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    /* At least some allocations should have succeeded. */
    ASSERT(p.vm.regs[1].i32 > 0);
    /* 3776 / 128 = 29 successful allocs. */
    ASSERT_INT_EQ(29, p.vm.regs[1].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Stack overflow via infinite recursion                             */
/* ======================================================================= */

static bool test_stack_overflow_recursion(void) {
    /* Small stack: 256 bytes. Each call frame = sizeof(aegis_register_t) = 16.
     * Max depth = 256/16 = 16 frames. Infinite recursion should error. */
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, 4096);

    /* 0: call self (PC=0) — infinite recursion */
    emit(&p, insn_irr(AEGIS_OP_CALL, 0, 0, 0));
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));  /* never reached */

    ASSERT(prog_build_ex(&p, 1000000, 64, 256));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Push overflow — push registers until stack is full                */
/* ======================================================================= */

static bool test_push_overflow(void) {
    /* Stack: 256 bytes. Each push = 16 bytes. Max = 16 pushes.
     * Loop pushing forever should error. */
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, 4096);

    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 42, 0));  /* 0: r0=42 */
    /* loop (PC=1): push r0 forever */
    emit(&p, insn_rrr(AEGIS_OP_PUSH, 0, 0, 0));        /* 1: push r0 */
    emit(&p, insn_irr(AEGIS_OP_JMP, 1, 0, 0));         /* 2: goto loop */

    ASSERT(prog_build_ex(&p, 1000000, 64, 256));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Heap store out of bounds → error                                  */
/* ======================================================================= */

static bool test_heap_store_oob(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, 4096);

    /* Alloc 32 bytes, then try to store at a massive offset. */
    emit(&p, insn_rir(AEGIS_OP_ALLOC, 0, 32, 0));         /* 0: r0=alloc(32) */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 999, 0));     /* 1: r1=999 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 9999, 0));    /* 2: r2=9999 (oob) */
    /* store r1 at base=r2, slot=0 → out of bounds */
    emit(&p, make_insn(AEGIS_OP_STORE, 0x04, 1, 2, 0));   /* 3: store oob */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build_ex(&p, 1000, 64, 256));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Static store out of bounds → error                                */
/* ======================================================================= */

static bool test_static_store_oob(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, 4096);

    /* Static = 64 bytes. Byte offset 64 is out of bounds. */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 42, 0));
    /* static_store at byte offset 64 (oob, only 64 bytes available) */
    emit(&p, insn_irr(AEGIS_OP_STATIC_STORE, 64, 0, 0));
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build_ex(&p, 1000, 64, 256));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Pop from empty stack → error                                      */
/* ======================================================================= */

static bool test_pop_empty_stack(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, 4096);

    /* Pop with nothing on the stack. */
    emit(&p, insn_rrr(AEGIS_OP_POP, 0, 0, 0));
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build_ex(&p, 1000, 64, 256));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Ret with empty call stack → error                                 */
/* ======================================================================= */

static bool test_ret_empty_call_stack(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, 4096);

    emit(&p, insn_rrr(AEGIS_OP_RET, 0, 0, 0));
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build_ex(&p, 1000, 64, 256));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Main                                                                    */
/* ======================================================================= */

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis VM Memory Exhaustion Tests ===\n\n");

    RUN(test_alloc_too_large);
    RUN(test_alloc_loop_exhaustion);
    RUN(test_stack_overflow_recursion);
    RUN(test_push_overflow);
    RUN(test_heap_store_oob);
    RUN(test_static_store_oob);
    RUN(test_pop_empty_stack);
    RUN(test_ret_empty_call_stack);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
