/**
 * @file aegis_vm_tests.c
 * @brief Integration tests for the Aegis VM interpreter loop.
 *
 * Tests small bytecode programs end-to-end: load → compute → yield/exit.
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
/* Helper: build instruction with immediate flags                          */
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

/* Convenience: no immediate flags. */
static aegis_instruction_t insn_rrr(aegis_opcode_t op,
                                    uint32_t a, uint32_t b, uint32_t c) {
    return make_insn(op, 0, a, b, c);
}

/* Convenience: B is immediate. */
static aegis_instruction_t insn_rir(aegis_opcode_t op,
                                    uint32_t a, uint32_t imm_b, uint32_t c) {
    return make_insn(op, 0x02, a, imm_b, c); /* bit 17 = IMM_B */
}

/* Convenience: A is immediate. */
static aegis_instruction_t insn_irr(aegis_opcode_t op,
                                    uint32_t imm_a, uint32_t b, uint32_t c) {
    return make_insn(op, 0x01, imm_a, b, c); /* bit 16 = IMM_A */
}

#define TEST_VM_ARENA_SIZE  8192
#define DEFAULT_PROG_CAP   4096

typedef struct test_program {
    aegis_instruction_t *insns;      /* heap-allocated instruction buffer */
    uint32_t             count;      /* instructions written */
    uint32_t             capacity;   /* allocated instruction slots */
    aegis_bytecode_t     bc;
    aegis_vm_t           vm;
    uint8_t             *arena;      /* heap-allocated VM memory arena */
    uint32_t             arena_size;
} test_program_t;

static void prog_alloc(test_program_t *p, uint32_t insn_cap,
                       uint32_t vm_arena) {
    memset(p, 0, sizeof(*p));
    p->capacity   = insn_cap;
    p->arena_size = vm_arena;
    p->insns = (aegis_instruction_t *)calloc(insn_cap, sizeof(aegis_instruction_t));
    p->arena = (uint8_t *)calloc(vm_arena, 1);
}

static void prog_free(test_program_t *p) {
    free(p->insns);
    free(p->arena);
    memset(p, 0, sizeof(*p));
}

static bool prog_build(test_program_t *p, uint32_t fuel) {
    memset(p->arena, 0, p->arena_size);
    memset(&p->vm, 0, sizeof(p->vm));
    aegis_bytecode_init(&p->bc);
    p->bc.instructions      = p->insns;
    p->bc.instruction_count = p->count;
    p->bc.static_size       = 64;

    aegis_config_t cfg = aegis_config_default();
    cfg.fuel_budget = fuel;
    cfg.stack_max   = 512;

    return aegis_vm_init(&p->vm, &p->bc, &cfg, p->arena, p->arena_size);
}

/* ======================================================================= */
/* Test: load_imm + add + yield                                            */
/* ======================================================================= */

static bool test_trivial_add_yield(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 10, 0);  /* r0 = 10 */
    p.insns[1] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 20, 0);  /* r1 = 20 */
    p.insns[2] = insn_rrr(AEGIS_OP_ADD, 2, 0, 1);         /* r2 = r0 + r1 */
    p.insns[3] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);       /* yield */
    p.count = 4;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(30, p.vm.regs[2].i32);
    ASSERT_INT_EQ(4, (int)p.vm.pc);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: exit with code                                                    */
/* ======================================================================= */

static bool test_exit_with_code(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 42, 0);
    p.insns[1] = insn_rrr(AEGIS_OP_EXIT, 0, 0, 0);
    p.count = 2;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_EXITED, (int)s);
    ASSERT_INT_EQ(42, (int)p.vm.exit_code);
    ASSERT(!p.vm.alive);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: conditional branch                                                */
/* ======================================================================= */

static bool test_conditional_branch(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 1, 0);
    p.insns[1] = insn_rir(AEGIS_OP_JMP_IF, 0, 3, 0);
    p.insns[2] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 99, 0);
    p.insns[3] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 42, 0);
    p.insns[4] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = 5;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(42, p.vm.regs[1].i32);
    prog_free(&p);
    return true;
}

static bool test_branch_not_taken(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0);
    p.insns[1] = insn_rir(AEGIS_OP_JMP_IF, 0, 3, 0);
    p.insns[2] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 99, 0);
    p.insns[3] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 42, 0);
    p.insns[4] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = 5;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(42, p.vm.regs[1].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: call and ret                                                      */
/* ======================================================================= */

static bool test_call_ret(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 10, 0);
    p.insns[1] = insn_irr(AEGIS_OP_CALL, 3, 0, 0);
    p.insns[2] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.insns[3] = insn_rrr(AEGIS_OP_ADD, 0, 0, 0);
    p.insns[4] = insn_rrr(AEGIS_OP_RET, 0, 0, 0);
    p.count = 5;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(20, p.vm.regs[0].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: fuel exhaustion causes force-yield                                */
/* ======================================================================= */

static bool test_fuel_exhaustion(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0);
    p.insns[1] = insn_irr(AEGIS_OP_JMP, 0, 0, 0);
    p.count = 2;
    ASSERT(prog_build(&p, 5));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_FORCE_YIELDED, (int)s);
    ASSERT(p.vm.alive);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: resume after force-yield continues from same PC                   */
/* ======================================================================= */

static bool test_resume_after_force_yield(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 1, 0);
    p.insns[1] = insn_rrr(AEGIS_OP_ADD, 0, 0, 0);
    p.insns[2] = insn_irr(AEGIS_OP_JMP, 1, 0, 0);
    p.count = 3;
    ASSERT(prog_build(&p, 4));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_FORCE_YIELDED, (int)s);
    ASSERT_INT_EQ(4, p.vm.regs[0].i32);
    ASSERT_INT_EQ(2, (int)p.vm.pc);

    /* Resume with more fuel. */
    p.vm.fuel = 3;
    s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_FORCE_YIELDED, (int)s);
    ASSERT_INT_EQ(8, p.vm.regs[0].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: unknown opcode → error                                            */
/* ======================================================================= */

static bool test_unknown_opcode(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    p.insns[0] = make_insn((aegis_opcode_t)0xFF, 0, 0, 0, 0);
    p.count = 1;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: unimplemented Phase 2 opcode → error                              */
/* ======================================================================= */

static bool test_unimplemented_opcode(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    /* Use a high opcode value (0x7F) that is guaranteed to be
     * unimplemented / unknown. */
    p.insns[0] = make_insn(0x7F, 0, 0, 0, 0);
    p.count = 1;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: PC out of bounds → error                                          */
/* ======================================================================= */

static bool test_pc_out_of_bounds(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 1, 0);
    p.count = 1;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: arithmetic program (sum 1..5)                                     */
/* ======================================================================= */

static bool test_sum_loop(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    int i = 0;
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0);
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 1, 0);
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 2, 6, 0);
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 4, 1, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_LT, 3, 1, 2);
    p.insns[i++] = insn_rir(AEGIS_OP_JMP_IF_NOT, 3, 9, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_ADD, 0, 0, 1);
    p.insns[i++] = insn_rrr(AEGIS_OP_ADD, 1, 1, 4);
    p.insns[i++] = insn_irr(AEGIS_OP_JMP, 4, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = (uint32_t)i;
    ASSERT(prog_build(&p, 10000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(15, p.vm.regs[0].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: div by zero → error                                               */
/* ======================================================================= */

static bool test_div_by_zero(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 10, 0);
    p.insns[1] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 0, 0);
    p.insns[2] = insn_rrr(AEGIS_OP_DIV, 2, 0, 1);
    p.count = 3;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: resume opcode is no-op                                            */
/* ======================================================================= */

static bool test_resume_noop(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    p.insns[0] = insn_rrr(AEGIS_OP_RESUME, 0, 0, 0);
    p.insns[1] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 77, 0);
    p.insns[2] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = 3;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(77, p.vm.regs[0].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: memory ops (alloc + store + load)                                 */
/* ======================================================================= */

static bool test_memory_ops(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    int i = 0;
    p.insns[i++] = insn_rir(AEGIS_OP_ALLOC, 0, 32, 0);
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 999, 0);
    p.insns[i++] = make_insn(AEGIS_OP_STORE, 0x04, 1, 0, 0);
    p.insns[i++] = make_insn(AEGIS_OP_LOAD, 0x04, 2, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = (uint32_t)i;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(999, p.vm.regs[2].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: static store persists across yield                                */
/* ======================================================================= */

static bool test_static_persist(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    int i = 0;
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 42, 0);
    p.insns[i++] = insn_irr(AEGIS_OP_STATIC_STORE, 0, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.insns[i++] = insn_rir(AEGIS_OP_STATIC_LOAD, 1, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = (uint32_t)i;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(3, (int)p.vm.pc);

    aegis_vm_reset_fuel(&p.vm);
    s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(42, p.vm.regs[1].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: jmp_if_not                                                        */
/* ======================================================================= */

static bool test_jmp_if_not(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    int i = 0;
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0);
    p.insns[i++] = insn_rir(AEGIS_OP_JMP_IF_NOT, 0, 3, 0);
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 99, 0);
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 55, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = (uint32_t)i;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(55, p.vm.regs[1].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: nested call/ret                                                   */
/* ======================================================================= */

static bool test_nested_call(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, TEST_VM_ARENA_SIZE);
    int i = 0;
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 1, 0);
    p.insns[i++] = insn_irr(AEGIS_OP_CALL, 4, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0); /* dead */
    p.insns[i++] = insn_irr(AEGIS_OP_CALL, 7, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_ADD, 0, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_RET, 0, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_ADD, 0, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_RET, 0, 0, 0);
    p.count = (uint32_t)i;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(4, p.vm.regs[0].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Main                                                                    */
/* ======================================================================= */

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis VM Integration Tests ===\n\n");

    RUN(test_trivial_add_yield);
    RUN(test_exit_with_code);
    RUN(test_conditional_branch);
    RUN(test_branch_not_taken);
    RUN(test_call_ret);
    RUN(test_fuel_exhaustion);
    RUN(test_resume_after_force_yield);
    RUN(test_unknown_opcode);
    RUN(test_unimplemented_opcode);
    RUN(test_pc_out_of_bounds);
    RUN(test_sum_loop);
    RUN(test_div_by_zero);
    RUN(test_resume_noop);
    RUN(test_memory_ops);
    RUN(test_static_persist);
    RUN(test_jmp_if_not);
    RUN(test_nested_call);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
