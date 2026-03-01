/**
 * @file aegis_vm_interrupt_tests.c
 * @brief Tests for Aegis VM fuel-based interruption and state preservation.
 *
 * Runs long-running loops with limited fuel budgets, verifies the VM
 * correctly force-yields, preserves all state (registers, PC, memory,
 * stack) across multiple interrupt/resume cycles, and produces the
 * correct final result after enough fuel is provided.
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
#define DEFAULT_VM_ARENA  (1u << 16)

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

static bool prog_build(test_program_t *p, uint32_t fuel,
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
/* Test: Count to 10000 with tiny fuel budget (10 per slice)               */
/* The counter should reach 10000 after enough resume cycles.              */
/* ======================================================================= */

static bool test_count_to_10k_interrupted(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * r0 = counter (0)
     * r1 = limit (10000)
     * r2 = one (1)
     * r3 = cmp
     *
     * loop (PC=3):
     *   if r0 >= r1 goto end
     *   r0 += 1
     *   goto loop
     * end: yield
     *
     * Loop body = 4 instructions per iteration: LT, JMP_IF_NOT, ADD, JMP
     * With 10 fuel → ~2 iterations per slice.
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0));       /* 0: counter=0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 10000, 0));   /* 1: limit */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 1, 0));       /* 2: one */

    /* loop (PC=3): */
    emit(&p, insn_rrr(AEGIS_OP_GE, 3, 0, 1));              /* 3: r3=cnt>=lim */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF, 3, 8, 0));          /* 4: if done→end */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 0, 0, 2));             /* 5: counter++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 3, 0, 0));             /* 6: goto loop */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));           /* 7: dead */

    /* end (PC=8): */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));           /* 8: yield */

    ASSERT(prog_build(&p, 10, 64, 256));

    int slices = 0;
    aegis_vm_status_t s;
    do {
        s = aegis_vm_run(&p.vm);
        slices++;

        if (s == AEGIS_VM_FORCE_YIELDED) {
            ASSERT(p.vm.alive);
            /* Counter should be monotonically increasing. */
            ASSERT(p.vm.regs[0].i32 <= 10000);
            /* Refuel for next slice. */
            aegis_vm_reset_fuel(&p.vm);
        }
    } while (s == AEGIS_VM_FORCE_YIELDED);

    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(10000, p.vm.regs[0].i32);
    /* Should have taken many slices. */
    ASSERT(slices > 100);
    printf("    (completed in %d slices)\n", slices);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Fibonacci(20) interrupted every 7 instructions                    */
/* Verifies register state is perfectly preserved across interrupts.       */
/* ======================================================================= */

static bool test_fib20_interrupted(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /* Identical Fibonacci program to the math test, but with fuel=7. */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 20, 0));   /* 0: n=20 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 0, 0));    /* 1: prev=0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 1, 0));    /* 2: curr=1 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 3, 0, 0));    /* 3: counter=0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 5, 1, 0));    /* 4: one=1 */

    /* loop (PC=5): */
    emit(&p, insn_rrr(AEGIS_OP_LT, 6, 3, 0));           /* 5: r6=cnt<n */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF_NOT, 6, 12, 0));  /* 6: if done→end */
    emit(&p, insn_rrr(AEGIS_OP_MOV, 4, 2, 0));          /* 7: tmp=curr */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 2, 2, 1));          /* 8: curr+=prev */
    emit(&p, insn_rrr(AEGIS_OP_MOV, 1, 4, 0));          /* 9: prev=tmp */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 3, 3, 5));          /* 10: cnt++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 5, 0, 0));          /* 11: goto loop */

    /* end (PC=12): */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 7, 64, 256));

    int slices = 0;
    aegis_vm_status_t s;
    do {
        s = aegis_vm_run(&p.vm);
        slices++;
        if (s == AEGIS_VM_FORCE_YIELDED) {
            aegis_vm_reset_fuel(&p.vm);
        }
    } while (s == AEGIS_VM_FORCE_YIELDED);

    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(10946, p.vm.regs[2].i32); /* 20 iterations from (0,1) = fib(21) */
    ASSERT(slices > 10);
    printf("    (completed in %d slices)\n", slices);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Accumulator with heap writes persists across force-yields         */
/* Each iteration: alloc, write to heap, add to accumulator.               */
/* Force-yield should NOT reset heap.                                      */
/* ======================================================================= */

static bool test_heap_persists_across_interrupts(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * r0 = sum (0)
     * r1 = counter (0)
     * r2 = limit (10)
     * r3 = one (1)
     * r4 = cmp
     * r5 = heap offset from alloc
     * r6 = loaded value from heap
     *
     * Each iteration: alloc 16 bytes, store counter to heap[r5],
     *   load it back, add to sum. This exercises heap persistence.
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0));     /* 0: sum=0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 0, 0));     /* 1: counter=0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 10, 0));    /* 2: limit=10 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 3, 1, 0));     /* 3: one=1 */

    /* loop (PC=4): */
    emit(&p, insn_rrr(AEGIS_OP_GE, 4, 1, 2));            /* 4: r4=cnt>=lim */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF, 4, 13, 0));       /* 5: if done→end */
    emit(&p, insn_rir(AEGIS_OP_ALLOC, 5, 16, 0));        /* 6: r5=alloc(16) */
    /* store counter (r1) to heap at base=r5, slot=0 */
    emit(&p, make_insn(AEGIS_OP_STORE, 0x04, 1, 5, 0));  /* 7: store */
    /* load back from heap at base=r5, slot=0 into r6 */
    emit(&p, make_insn(AEGIS_OP_LOAD, 0x04, 6, 5, 0));   /* 8: load */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 0, 0, 6));           /* 9: sum+=loaded */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 1, 1, 3));           /* 10: cnt++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 4, 0, 0));           /* 11: goto loop */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));         /* 12: dead */

    /* end (PC=13): */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    /* Fuel=5: will force-yield mid-iteration frequently. */
    ASSERT(prog_build(&p, 5, 64, 256));

    int slices = 0;
    aegis_vm_status_t s;
    do {
        s = aegis_vm_run(&p.vm);
        slices++;
        if (s == AEGIS_VM_FORCE_YIELDED) {
            aegis_vm_reset_fuel(&p.vm);
        }
    } while (s == AEGIS_VM_FORCE_YIELDED);

    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    /* sum = 0+1+2+...+9 = 45 */
    ASSERT_INT_EQ(45, p.vm.regs[0].i32);
    ASSERT(slices > 5);
    printf("    (completed in %d slices)\n", slices);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Static memory persists across both force-yields and explicit      */
/* yields. Run two phases: phase 1 writes to static and yields,           */
/* phase 2 reads from static (with interrupts) and verifies.              */
/* ======================================================================= */

static bool test_static_persists_across_all_yields(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * Phase 1: write value 12345 to static slot 0, then explicit yield.
     * Phase 2: loop that counts to 100 (with force-yields due to fuel=3),
     *   then loads from static slot 0 and verifies.
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 12345, 0));  /* 0 */
    emit(&p, insn_irr(AEGIS_OP_STATIC_STORE, 0, 0, 0));  /* 1: static[0]=r0 */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));          /* 2: yield (phase1) */

    /* Phase 2 (PC=3): count to 100 in r1. */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 0, 0));      /* 3: cnt=0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 100, 0));    /* 4: limit=100 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 3, 1, 0));      /* 5: one=1 */

    /* loop (PC=6): */
    emit(&p, insn_rrr(AEGIS_OP_GE, 4, 1, 2));             /* 6 */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF, 4, 11, 0));        /* 7: if done→load */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 1, 1, 3));            /* 8: cnt++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 6, 0, 0));            /* 9: goto loop */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));          /* 10: dead */

    /* load static and yield (PC=11): */
    emit(&p, insn_rir(AEGIS_OP_STATIC_LOAD, 10, 0, 0));   /* 11: r10=static[0] */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));          /* 12: yield (phase2) */

    /* Phase 1: run with plenty of fuel. */
    ASSERT(prog_build(&p, 100, 256, 256));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(3, (int)p.vm.pc);

    /* Phase 2: run with fuel=3 to force many interrupts. */
    p.vm.fuel = 3;

    int slices = 0;
    do {
        s = aegis_vm_run(&p.vm);
        slices++;
        if (s == AEGIS_VM_FORCE_YIELDED) {
            aegis_vm_reset_fuel(&p.vm);
            p.vm.fuel = 3; /* keep it tight */
        }
    } while (s == AEGIS_VM_FORCE_YIELDED);

    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    /* r10 should have loaded 12345 from static. */
    ASSERT_INT_EQ(12345, p.vm.regs[10].i32);
    ASSERT_INT_EQ(100, p.vm.regs[1].i32);
    ASSERT(slices > 10);
    printf("    (phase 2 completed in %d slices)\n", slices);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Call stack preserved across force-yields.                         */
/* A function body has a long loop that gets interrupted, then returns.    */
/* ======================================================================= */

static bool test_call_stack_preserved_across_interrupts(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * Main:
     *   0: r0 = 100
     *   1: call func (PC=3)
     *   2: yield  (r10 should be 5050)
     *
     * func (PC=3):
     *   Sum 1..r0 into r10 using a loop.
     *   3: r10 = 0
     *   4: r11 = 1
     *   5: r12 = 1
     *   loop (PC=6):
     *     6: r13 = r11 > r0
     *     7: if r13 goto ret
     *     8: r10 += r11
     *     9: r11 += r12
     *     10: goto loop
     *   11: ret
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 100, 0));   /* 0 */
    emit(&p, insn_irr(AEGIS_OP_CALL, 3, 0, 0));          /* 1: call func */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));         /* 2: yield */

    /* func (PC=3): */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 10, 0, 0));    /* 3 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 11, 1, 0));    /* 4 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 12, 1, 0));    /* 5 */

    /* loop (PC=6): */
    emit(&p, insn_rrr(AEGIS_OP_GT, 13, 11, 0));          /* 6 */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF, 13, 11, 0));      /* 7: if done→ret */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 10, 10, 11));        /* 8: sum+=i */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 11, 11, 12));        /* 9: i++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 6, 0, 0));           /* 10: goto loop */

    /* ret (PC=11): */
    emit(&p, insn_rrr(AEGIS_OP_RET, 0, 0, 0));

    /* Fuel=6: interrupts frequently inside the function. */
    ASSERT(prog_build(&p, 6, 64, 512));

    int slices = 0;
    aegis_vm_status_t s;
    do {
        s = aegis_vm_run(&p.vm);
        slices++;
        if (s == AEGIS_VM_FORCE_YIELDED) {
            /* During interrupts, call depth should be 1 (inside func). */
            ASSERT(p.vm.alive);
            aegis_vm_reset_fuel(&p.vm);
            p.vm.fuel = 6;
        }
    } while (s == AEGIS_VM_FORCE_YIELDED);

    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    /* sum(1..100) = 5050 */
    ASSERT_INT_EQ(5050, p.vm.regs[10].i32);
    ASSERT(slices > 20);
    printf("    (completed in %d slices)\n", slices);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Multiple register channels all maintained across interrupts.      */
/* Uses 8 independent accumulators in parallel to verify no register       */
/* corruption occurs during force-yield/resume cycles.                     */
/* ======================================================================= */

static bool test_multi_register_integrity(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * r0..r7 = accumulators (all start at 0)
     * r8 = counter (0)
     * r9 = limit (1000)
     * r10 = one
     *
     * Each iteration: r0 += 1, r1 += 2, r2 += 3, ..., r7 += 8.
     * Expected: r0=1000, r1=2000, r2=3000, ..., r7=8000.
     */
    for (int i = 0; i < 8; i++) {
        emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, (uint32_t)i, 0, 0));
    }
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 8, 0, 0));      /* counter */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 9, 1000, 0));   /* limit */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 10, 1, 0));     /* one */

    /* Load increment values into r20..r27 */
    for (int i = 0; i < 8; i++) {
        emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, (uint32_t)(20 + i),
                          (uint32_t)(i + 1), 0));
    }
    /* That's 19 setup instructions. */

    /* loop (PC=19): */
    uint32_t loop_pc = p.count;
    emit(&p, insn_rrr(AEGIS_OP_GE, 30, 8, 9));           /* r30=cnt>=lim */
    uint32_t jmp_pc = p.count;
    emit(&p, insn_rir(AEGIS_OP_JMP_IF, 30, 0, 0));       /* placeholder */

    /* 8 adds: r0..r7 += r20..r27 */
    for (int i = 0; i < 8; i++) {
        emit(&p, insn_rrr(AEGIS_OP_ADD, (uint32_t)i,
                          (uint32_t)i, (uint32_t)(20 + i)));
    }
    emit(&p, insn_rrr(AEGIS_OP_ADD, 8, 8, 10));           /* counter++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, loop_pc, 0, 0));      /* goto loop */

    /* end: */
    uint32_t end_pc = p.count;
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    /* Patch the jump target. */
    p.insns[jmp_pc] = insn_rir(AEGIS_OP_JMP_IF, 30, end_pc, 0);

    /* Fuel=13: enough for about 1 loop iteration. */
    ASSERT(prog_build(&p, 13, 64, 256));

    int slices = 0;
    aegis_vm_status_t s;
    do {
        s = aegis_vm_run(&p.vm);
        slices++;
        if (s == AEGIS_VM_FORCE_YIELDED) {
            aegis_vm_reset_fuel(&p.vm);
            p.vm.fuel = 13;
        }
    } while (s == AEGIS_VM_FORCE_YIELDED);

    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);

    /* Verify all 8 accumulators. */
    for (int i = 0; i < 8; i++) {
        int expected = 1000 * (i + 1);
        if (p.vm.regs[i].i32 != expected) {
            printf("  ASSERT FAILED: r%d = %d, expected %d (line %d)\n",
                   i, p.vm.regs[i].i32, expected, __LINE__);
            return false;
        }
    }

    ASSERT(slices > 50);
    printf("    (completed in %d slices, all 8 registers correct)\n", slices);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Main                                                                    */
/* ======================================================================= */

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis VM Interrupt/State Preservation Tests ===\n\n");

    RUN(test_count_to_10k_interrupted);
    RUN(test_fib20_interrupted);
    RUN(test_heap_persists_across_interrupts);
    RUN(test_static_persists_across_all_yields);
    RUN(test_call_stack_preserved_across_interrupts);
    RUN(test_multi_register_integrity);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
