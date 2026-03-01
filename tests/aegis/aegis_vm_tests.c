/**
 * @file aegis_vm_tests.c
 * @brief Integration tests for the Aegis VM interpreter loop.
 *
 * Tests small bytecode programs end-to-end: load → compute → yield/exit.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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

#define TEST_ARENA_SIZE 8192
#define MAX_PROG 32

typedef struct test_program {
    aegis_instruction_t insns[MAX_PROG];
    uint32_t            count;
    aegis_bytecode_t    bc;
    aegis_vm_t          vm;
    uint8_t             arena[TEST_ARENA_SIZE];
} test_program_t;

static bool prog_init(test_program_t *p, uint32_t fuel) {
    /* Zero arena and VM state but NOT instructions (already populated). */
    memset(p->arena, 0, TEST_ARENA_SIZE);
    memset(&p->vm, 0, sizeof(p->vm));
    aegis_bytecode_init(&p->bc);
    p->bc.instructions      = p->insns;
    p->bc.instruction_count = p->count;
    p->bc.static_size       = 64;

    aegis_config_t cfg = aegis_config_default();
    cfg.fuel_budget = fuel;
    cfg.stack_max   = 512;

    return aegis_vm_init(&p->vm, &p->bc, &cfg, p->arena, TEST_ARENA_SIZE);
}

/* Set instruction count before init. */
static bool prog_build(test_program_t *p, uint32_t fuel) {
    p->bc.instruction_count = p->count;
    return prog_init(p, fuel);
}

/* ======================================================================= */
/* Test: load_imm + add + yield                                            */
/* ======================================================================= */

static bool test_trivial_add_yield(void) {
    test_program_t p;
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 10, 0);  /* r0 = 10 */
    p.insns[1] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 20, 0);  /* r1 = 20 */
    p.insns[2] = insn_rrr(AEGIS_OP_ADD, 2, 0, 1);         /* r2 = r0 + r1 */
    p.insns[3] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);       /* yield */
    p.count = 4;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(30, p.vm.regs[2].i32);
    ASSERT_INT_EQ(4, (int)p.vm.pc); /* PC advanced past yield */
    return true;
}

/* ======================================================================= */
/* Test: exit with code                                                    */
/* ======================================================================= */

static bool test_exit_with_code(void) {
    test_program_t p;
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 42, 0); /* r0 = 42 */
    p.insns[1] = insn_rrr(AEGIS_OP_EXIT, 0, 0, 0);      /* exit(r0) */
    p.count = 2;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_EXITED, (int)s);
    ASSERT_INT_EQ(42, (int)p.vm.exit_code);
    ASSERT(!p.vm.alive);
    return true;
}

/* ======================================================================= */
/* Test: conditional branch                                                */
/* ======================================================================= */

static bool test_conditional_branch(void) {
    test_program_t p;
    /* r0 = 1 (truthy) */
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 1, 0);
    /* jmp_if r0, label=3 */
    p.insns[1] = insn_rir(AEGIS_OP_JMP_IF, 0, 3, 0);
    /* r1 = 99 (should be skipped) */
    p.insns[2] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 99, 0);
    /* r1 = 42 (branch target) */
    p.insns[3] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 42, 0);
    p.insns[4] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = 5;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(42, p.vm.regs[1].i32); /* skipped the r1=99 */
    return true;
}

static bool test_branch_not_taken(void) {
    test_program_t p;
    /* r0 = 0 (falsy) */
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0);
    /* jmp_if r0, label=3 — NOT taken */
    p.insns[1] = insn_rir(AEGIS_OP_JMP_IF, 0, 3, 0);
    /* r1 = 99 (should execute) */
    p.insns[2] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 99, 0);
    /* r1 = 42 (also executes, overwrites) */
    p.insns[3] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 42, 0);
    p.insns[4] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = 5;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    /* Both instructions executed, r1 = 42 (last write wins) */
    ASSERT_INT_EQ(42, p.vm.regs[1].i32);
    return true;
}

/* ======================================================================= */
/* Test: call and ret                                                      */
/* ======================================================================= */

static bool test_call_ret(void) {
    test_program_t p;
    /* 0: r0 = 10 */
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 10, 0);
    /* 1: call label=3 */
    p.insns[1] = insn_irr(AEGIS_OP_CALL, 3, 0, 0);
    /* 2: yield (return point) */
    p.insns[2] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    /* 3: r0 = r0 + r0 (function body: doubles r0) */
    p.insns[3] = insn_rrr(AEGIS_OP_ADD, 0, 0, 0);
    /* 4: ret */
    p.insns[4] = insn_rrr(AEGIS_OP_RET, 0, 0, 0);
    p.count = 5;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(20, p.vm.regs[0].i32); /* 10 + 10 = 20 */
    return true;
}

/* ======================================================================= */
/* Test: fuel exhaustion causes force-yield                                */
/* ======================================================================= */

static bool test_fuel_exhaustion(void) {
    test_program_t p;
    /* Infinite loop: jmp 0 */
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0);
    p.insns[1] = insn_irr(AEGIS_OP_JMP, 0, 0, 0);
    p.count = 2;
    ASSERT(prog_build(&p, 5)); /* only 5 fuel */

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_FORCE_YIELDED, (int)s);
    ASSERT(p.vm.alive); /* not dead, just paused */
    return true;
}

/* ======================================================================= */
/* Test: resume after force-yield continues from same PC                   */
/* ======================================================================= */

static bool test_resume_after_force_yield(void) {
    test_program_t p;
    /* 0: r0 = 1 */
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 1, 0);
    /* 1: r0 = r0 + r0 (doubles each time) */
    p.insns[1] = insn_rrr(AEGIS_OP_ADD, 0, 0, 0);
    /* 2: jmp 1 (infinite loop on add) */
    p.insns[2] = insn_irr(AEGIS_OP_JMP, 1, 0, 0);
    p.count = 3;
    ASSERT(prog_build(&p, 4)); /* 4 fuel: executes insns 0,1,2,1 then exhausts */

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_FORCE_YIELDED, (int)s);
    /* r0 was: 1 → 2 → (jmp) → 4. 4 instructions: load_imm, add, jmp, add.
     * On the 4th instruction (add), r0 = 2+2 = 4. Then fuel exhausted.
     * Actually let's trace: fuel=4
     * PC=0: load_imm r0=1 (fuel=3, pc=1)
     * PC=1: add r0=1+1=2 (fuel=2, pc=2)
     * PC=2: jmp 1 (fuel=1, pc=1)
     * PC=1: add r0=2+2=4 (fuel=0, pc=2)
     * Next iteration: fuel check fails → force-yield at pc=2
     */
    ASSERT_INT_EQ(4, p.vm.regs[0].i32);

    /* Resume: VM should continue from PC=2 (the jmp). */
    ASSERT_INT_EQ(2, (int)p.vm.pc);
    aegis_vm_reset_fuel(&p.vm);
    /* Give 3 more fuel: jmp, add, (exhaust on next) */
    p.vm.fuel = 3;
    s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_FORCE_YIELDED, (int)s);
    /* PC=2: jmp 1 (fuel=2, pc=1)
     * PC=1: add r0=4+4=8 (fuel=1, pc=2)
     * PC=2: jmp 1 (fuel=0, pc=1)
     * Next: fuel check fails → force-yield at pc=1
     */
    ASSERT_INT_EQ(8, p.vm.regs[0].i32);
    return true;
}

/* ======================================================================= */
/* Test: unknown opcode → error                                            */
/* ======================================================================= */

static bool test_unknown_opcode(void) {
    test_program_t p;
    /* Opcode 0xFF doesn't exist. */
    p.insns[0] = make_insn((aegis_opcode_t)0xFF, 0, 0, 0, 0);
    p.count = 1;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    return true;
}

/* ======================================================================= */
/* Test: unimplemented Phase 2 opcode → error                              */
/* ======================================================================= */

static bool test_unimplemented_opcode(void) {
    test_program_t p;
    p.insns[0] = make_insn(AEGIS_OP_WAIT, 0, 0, 0, 0);
    p.count = 1;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    return true;
}

/* ======================================================================= */
/* Test: PC out of bounds → error                                          */
/* ======================================================================= */

static bool test_pc_out_of_bounds(void) {
    test_program_t p;
    /* A single instruction that doesn't yield — PC will go past end. */
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 1, 0);
    p.count = 1;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    /* After executing insn 0, PC becomes 1, which is out of bounds. */
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    return true;
}

/* ======================================================================= */
/* Test: arithmetic program (sum 1..5)                                     */
/* ======================================================================= */

static bool test_sum_loop(void) {
    test_program_t p;
    /* Sum integers 1..5 using a loop.
     * r0 = sum (accumulator)
     * r1 = counter (starts at 1)
     * r2 = limit (6)
     * r3 = temp (comparison result)
     * r4 = increment (1)
     */
    int i = 0;
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0);  /* r0 = 0 */
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 1, 0);  /* r1 = 1 */
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 2, 6, 0);  /* r2 = 6 */
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 4, 1, 0);  /* r4 = 1 */
    /* loop: */
    p.insns[i++] = insn_rrr(AEGIS_OP_LT, 3, 1, 2);        /* r3 = r1 < r2 */
    p.insns[i++] = insn_rir(AEGIS_OP_JMP_IF_NOT, 3, 9, 0); /* if !r3, goto end */
    p.insns[i++] = insn_rrr(AEGIS_OP_ADD, 0, 0, 1);        /* r0 += r1 */
    p.insns[i++] = insn_rrr(AEGIS_OP_ADD, 1, 1, 4);        /* r1 += 1 */
    p.insns[i++] = insn_irr(AEGIS_OP_JMP, 4, 0, 0);        /* goto loop */
    /* end: */
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = (uint32_t)i;
    ASSERT(prog_build(&p, 10000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(15, p.vm.regs[0].i32); /* 1+2+3+4+5 = 15 */
    return true;
}

/* ======================================================================= */
/* Test: div by zero → error                                               */
/* ======================================================================= */

static bool test_div_by_zero(void) {
    test_program_t p;
    p.insns[0] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 10, 0); /* r0 = 10 */
    p.insns[1] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 0, 0);  /* r1 = 0 */
    p.insns[2] = insn_rrr(AEGIS_OP_DIV, 2, 0, 1);       /* r2 = r0 / r1 */
    p.count = 3;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    return true;
}

/* ======================================================================= */
/* Test: resume opcode is no-op                                            */
/* ======================================================================= */

static bool test_resume_noop(void) {
    test_program_t p;
    p.insns[0] = insn_rrr(AEGIS_OP_RESUME, 0, 0, 0);
    p.insns[1] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 77, 0);
    p.insns[2] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = 3;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(77, p.vm.regs[0].i32);
    return true;
}

/* ======================================================================= */
/* Test: memory ops (alloc + store + load)                                 */
/* ======================================================================= */

static bool test_memory_ops(void) {
    test_program_t p;
    /* r0 = alloc(32)
     * r1 = 999
     * store r1 to heap at r0, slot 0
     * load r2 from heap at r0, slot 0
     * yield
     */
    int i = 0;
    /* alloc: A=dest reg, B=size (immediate) */
    p.insns[i++] = insn_rir(AEGIS_OP_ALLOC, 0, 32, 0);
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 999, 0);
    /* store: A=value(r1), B=base(r0), C=slot(0 imm) */
    p.insns[i++] = make_insn(AEGIS_OP_STORE, 0x04, 1, 0, 0); /* IMM_C=bit18 */
    /* load: A=dest(r2), B=base(r0), C=slot(0 imm) */
    p.insns[i++] = make_insn(AEGIS_OP_LOAD, 0x04, 2, 0, 0); /* IMM_C=bit18 */
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = (uint32_t)i;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(999, p.vm.regs[2].i32);
    return true;
}

/* ======================================================================= */
/* Test: static store persists across yield                                */
/* ======================================================================= */

static bool test_static_persist(void) {
    test_program_t p;
    int i = 0;
    /* r0 = 42, static_store slot=0, r0 */
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 42, 0);
    /* static_store: A=slot(0 imm), B=value(r0) */
    p.insns[i++] = insn_irr(AEGIS_OP_STATIC_STORE, 0, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    /* After yield + resume: static_load r1, slot 0 */
    p.insns[i++] = insn_rir(AEGIS_OP_STATIC_LOAD, 1, 0, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = (uint32_t)i;
    ASSERT(prog_build(&p, 1000));

    /* First run: store and yield. */
    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(3, (int)p.vm.pc); /* past first yield */

    /* Resume: load from static and yield. */
    aegis_vm_reset_fuel(&p.vm);
    s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(42, p.vm.regs[1].i32);
    return true;
}

/* ======================================================================= */
/* Test: jmp_if_not                                                        */
/* ======================================================================= */

static bool test_jmp_if_not(void) {
    test_program_t p;
    /* r0 = 0, jmp_if_not r0, label=2, r1=77, yield */
    int i = 0;
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0);
    p.insns[i++] = insn_rir(AEGIS_OP_JMP_IF_NOT, 0, 3, 0);
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 99, 0); /* skipped */
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 1, 55, 0);
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);
    p.count = (uint32_t)i;
    ASSERT(prog_build(&p, 1000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(55, p.vm.regs[1].i32);
    return true;
}

/* ======================================================================= */
/* Test: nested call/ret                                                   */
/* ======================================================================= */

static bool test_nested_call(void) {
    test_program_t p;
    /* 0: r0 = 1 */
    /* 1: call func_a (=4) */
    /* 2: yield */
    /* 3: (dead) */
    /* 4: func_a: call func_b (=7) */
    /* 5: r0 = r0 + r0 */
    /* 6: ret */
    /* 7: func_b: r0 = r0 + r0 */
    /* 8: ret */
    int i = 0;
    p.insns[i++] = insn_rir(AEGIS_OP_LOAD_IMM, 0, 1, 0);  /* 0: r0=1 */
    p.insns[i++] = insn_irr(AEGIS_OP_CALL, 4, 0, 0);       /* 1: call 4 */
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);      /* 2: yield */
    p.insns[i++] = insn_rrr(AEGIS_OP_YIELD, 0, 0, 0);      /* 3: dead */
    p.insns[i++] = insn_irr(AEGIS_OP_CALL, 7, 0, 0);       /* 4: call 7 */
    p.insns[i++] = insn_rrr(AEGIS_OP_ADD, 0, 0, 0);        /* 5: r0+=r0 */
    p.insns[i++] = insn_rrr(AEGIS_OP_RET, 0, 0, 0);        /* 6: ret */
    p.insns[i++] = insn_rrr(AEGIS_OP_ADD, 0, 0, 0);        /* 7: r0+=r0 */
    p.insns[i++] = insn_rrr(AEGIS_OP_RET, 0, 0, 0);        /* 8: ret */
    p.count = (uint32_t)i;
    ASSERT(prog_build(&p, 1000));

    /* Execution: 0→1→4→7→(r0=2)→8(ret→5)→(r0=4)→6(ret→2)→yield */
    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(4, p.vm.regs[0].i32); /* 1 → 2 (func_b) → 4 (func_a) */
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
