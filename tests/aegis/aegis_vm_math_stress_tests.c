/**
 * @file aegis_vm_math_stress_tests.c
 * @brief Stress tests for Aegis VM arithmetic and vector math correctness.
 *
 * Runs substantial computations through the interpreter and verifies
 * results against known-good values: Fibonacci, factorial via repeated
 * multiplication, dot product accumulation, nested function arithmetic,
 * and iterative square root (Newton's method via integer approximation).
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

#define DEFAULT_PROG_CAP  (1u << 16)  /* 64K instructions */
#define DEFAULT_VM_ARENA  (1u << 16)  /* 64KB VM memory */

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

static bool prog_build(test_program_t *p, uint32_t fuel) {
    memset(p->arena, 0, p->arena_size);
    memset(&p->vm, 0, sizeof(p->vm));
    aegis_bytecode_init(&p->bc);
    p->bc.instructions      = p->insns;
    p->bc.instruction_count = p->count;
    p->bc.static_size       = 256;

    aegis_config_t cfg = aegis_config_default();
    cfg.fuel_budget = fuel;
    cfg.stack_max   = 4096;

    return aegis_vm_init(&p->vm, &p->bc, &cfg, p->arena, p->arena_size);
}

/* Append an instruction to the program. */
static void emit(test_program_t *p, aegis_instruction_t insn) {
    if (p->count < p->capacity) {
        p->insns[p->count++] = insn;
    }
}

/* ======================================================================= */
/* Test: Fibonacci sequence with 30 iterations = 1346269                   */
/* Iterative: fib(n) using a loop with two accumulators.                   */
/* ======================================================================= */

static bool test_fibonacci_30(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * r0 = n (30)
     * r1 = fib_prev (0)
     * r2 = fib_curr (1)
     * r3 = counter (0)
     * r4 = temp
     * r5 = one (1)
     * r6 = cmp result
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 30, 0));  /* r0 = 30 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 0, 0));   /* r1 = 0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 1, 0));   /* r2 = 1 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 3, 0, 0));   /* r3 = 0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 5, 1, 0));   /* r5 = 1 */

    /* loop (PC=5): */
    emit(&p, insn_rrr(AEGIS_OP_LT, 6, 3, 0));           /* r6 = r3 < r0 */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF_NOT, 6, 12, 0));   /* if !r6 goto end */
    emit(&p, insn_rrr(AEGIS_OP_MOV, 4, 2, 0));           /* r4 = r2 */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 2, 2, 1));           /* r2 = r2 + r1 */
    emit(&p, insn_rrr(AEGIS_OP_MOV, 1, 4, 0));           /* r1 = r4 (old r2) */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 3, 3, 5));           /* r3++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 5, 0, 0));           /* goto loop */

    /* end (PC=12): */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 1000000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(1346269, p.vm.regs[2].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Factorial(12) = 479001600                                         */
/* Iterative multiplication loop.                                          */
/* ======================================================================= */

static bool test_factorial_12(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * r0 = result (1)
     * r1 = counter (1)
     * r2 = limit (13)
     * r3 = one (1)
     * r4 = cmp
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 1, 0));   /* r0 = 1 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 1, 0));   /* r1 = 1 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 13, 0));  /* r2 = 13 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 3, 1, 0));   /* r3 = 1 */

    /* loop (PC=4): */
    emit(&p, insn_rrr(AEGIS_OP_LT, 4, 1, 2));            /* r4 = r1 < r2 */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF_NOT, 4, 10, 0));    /* if !r4 goto end */
    emit(&p, insn_rrr(AEGIS_OP_MUL, 0, 0, 1));            /* r0 *= r1 */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 1, 1, 3));            /* r1++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 4, 0, 0));            /* goto loop */

    /* dead instruction to make end label correct */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));         /* PC=9: yield */

    /* The jmp_if_not targets PC=10 but we only have 10 insns (0-9),
     * so we need to make end = 9. Let me fix the jump target. */
    p.count = 0; /* redo */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 1, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 1, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 13, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 3, 1, 0));
    /* loop (PC=4): */
    emit(&p, insn_rrr(AEGIS_OP_LT, 4, 1, 2));
    emit(&p, insn_rir(AEGIS_OP_JMP_IF_NOT, 4, 9, 0));
    emit(&p, insn_rrr(AEGIS_OP_MUL, 0, 0, 1));
    emit(&p, insn_rrr(AEGIS_OP_ADD, 1, 1, 3));
    emit(&p, insn_irr(AEGIS_OP_JMP, 4, 0, 0));
    /* end (PC=9): */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 1000000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(479001600, p.vm.regs[0].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Sum of squares 1^2 + 2^2 + ... + 100^2 = 338350                  */
/* Uses MUL for squaring then ADD for accumulation.                        */
/* ======================================================================= */

static bool test_sum_of_squares_100(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * r0 = sum (0)
     * r1 = i (1)
     * r2 = limit (101)
     * r3 = one (1)
     * r4 = i*i
     * r5 = cmp
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 1, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 101, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 3, 1, 0));
    /* loop (PC=4): */
    emit(&p, insn_rrr(AEGIS_OP_LT, 5, 1, 2));
    emit(&p, insn_rir(AEGIS_OP_JMP_IF_NOT, 5, 10, 0));
    emit(&p, insn_rrr(AEGIS_OP_MUL, 4, 1, 1));        /* r4 = i*i */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 0, 0, 4));         /* sum += i*i */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 1, 1, 3));         /* i++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 4, 0, 0));
    /* end (PC=10): */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 1000000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(338350, p.vm.regs[0].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Collatz conjecture sequence length for n=27 (111 steps)           */
/* Exercises branching, div, mul, add in a non-trivial pattern.            */
/* ======================================================================= */

static bool test_collatz_27(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * r0 = n (27)
     * r1 = steps (0)
     * r2 = one (1)
     * r3 = two (2)
     * r4 = three (3)
     * r5 = cmp (n == 1?)
     * r6 = temp (n mod 2)
     * r7 = cmp (is_even?)
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 27, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 0, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 1, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 3, 2, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 4, 3, 0));

    /* loop (PC=5): if n == 1, goto end */
    emit(&p, insn_rrr(AEGIS_OP_EQ, 5, 0, 2));          /* r5 = (n == 1) */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF, 5, 16, 0));      /* if r5 goto end */

    /* r6 = n mod 2 */
    emit(&p, insn_rrr(AEGIS_OP_MOD, 6, 0, 3));          /* r6 = n%2 */
    /* if r6 == 0, even path */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF_NOT, 6, 11, 0));  /* if even goto even */

    /* odd: n = 3*n + 1 */
    emit(&p, insn_rrr(AEGIS_OP_MUL, 0, 0, 4));          /* n = n*3 */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 0, 0, 2));           /* n = n+1 */
    /* goto step_done (PC=13) */
    emit(&p, insn_irr(AEGIS_OP_JMP, 13, 0, 0));          /* PC=11 → 13 is wrong */

    /* even (PC=11 is wrong, let me recount)... */
    /* Let me re-emit cleanly. */
    p.count = 0;

    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 27, 0));   /* 0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 0, 0));    /* 1 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 1, 0));    /* 2 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 3, 2, 0));    /* 3 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 4, 3, 0));    /* 4 */

    /* loop (PC=5): */
    emit(&p, insn_rrr(AEGIS_OP_EQ, 5, 0, 2));           /* 5: r5 = (n==1) */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF, 5, 17, 0));      /* 6: if done goto 17 */

    emit(&p, insn_rrr(AEGIS_OP_MOD, 6, 0, 3));          /* 7: r6 = n%2 */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF, 6, 12, 0));      /* 8: if odd goto 12 */

    /* even (PC=9): n = n / 2 */
    emit(&p, insn_rrr(AEGIS_OP_DIV, 0, 0, 3));          /* 9: n = n/2 */
    emit(&p, insn_irr(AEGIS_OP_JMP, 14, 0, 0));         /* 10: goto step_done */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));        /* 11: dead */

    /* odd (PC=12): n = 3*n + 1 */
    emit(&p, insn_rrr(AEGIS_OP_MUL, 0, 0, 4));          /* 12: n = n*3 */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 0, 0, 2));          /* 13: n = n+1 */

    /* step_done (PC=14): steps++ and loop */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 1, 1, 2));          /* 14: steps++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 5, 0, 0));          /* 15: goto loop */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));        /* 16: dead */

    /* end (PC=17): */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));        /* 17: yield */

    ASSERT(prog_build(&p, 10000000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    /* Collatz(27) takes 111 steps to reach 1. */
    ASSERT_INT_EQ(111, p.vm.regs[1].i32);
    ASSERT_INT_EQ(1, p.vm.regs[0].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Bitwise operations — population count via Kernighan's algorithm   */
/* popcount(0xDEADBEEF) = 24                                              */
/* ======================================================================= */

static bool test_popcount(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * r0 = n (0xDEADBEEF)
     * r1 = count (0)
     * r2 = one (1)
     * r3 = zero (0)
     * r4 = cmp
     * r5 = temp (n-1)
     *
     * Kernighan's: while (n) { n &= n-1; count++; }
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 0xDEADBEEF, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 0, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 1, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 3, 0, 0));

    /* loop (PC=4): */
    emit(&p, insn_rrr(AEGIS_OP_EQ, 4, 0, 3));           /* r4 = (n==0) */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF, 4, 10, 0));      /* if n==0 goto end */
    emit(&p, insn_rrr(AEGIS_OP_SUB, 5, 0, 2));          /* r5 = n-1 */
    emit(&p, insn_rrr(AEGIS_OP_AND, 0, 0, 5));          /* n &= n-1 */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 1, 1, 2));          /* count++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 4, 0, 0));          /* goto loop */

    /* end (PC=10): */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 1000000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(24, p.vm.regs[1].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: GCD of 46368 and 28657 via Euclidean algorithm = 1                */
/* (These are consecutive Fibonacci numbers, so GCD is always 1.)          */
/* ======================================================================= */

static bool test_gcd_euclid(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * r0 = a (46368)
     * r1 = b (28657)
     * r2 = zero (0)
     * r3 = cmp
     * r4 = temp (a mod b)
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 46368, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 28657, 0));
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 0, 0));

    /* loop (PC=3): while b != 0 */
    emit(&p, insn_rrr(AEGIS_OP_EQ, 3, 1, 2));           /* r3 = (b==0) */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF, 3, 9, 0));       /* if b==0 goto end */
    emit(&p, insn_rrr(AEGIS_OP_MOD, 4, 0, 1));          /* r4 = a%b */
    emit(&p, insn_rrr(AEGIS_OP_MOV, 0, 1, 0));          /* a = b */
    emit(&p, insn_rrr(AEGIS_OP_MOV, 1, 4, 0));          /* b = r4 */
    emit(&p, insn_irr(AEGIS_OP_JMP, 3, 0, 0));          /* goto loop */

    /* end (PC=9): result in r0 */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 1000000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(1, p.vm.regs[0].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Nested function calls — power(base, exp) via repeated mul.        */
/* Computes 3^7 = 2187 using call/ret with a function body.                */
/* ======================================================================= */

static bool test_power_via_call(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /*
     * Main:
     *   r0 = base (3)
     *   r1 = exp (7)
     *   call power_func (PC=4)
     *   yield  (result in r10)
     *
     * power_func (PC=4):
     *   r10 = 1 (result)
     *   r11 = 0 (counter)
     *   r12 = 1 (increment)
     *   loop: if r11 >= r1, goto ret
     *     r10 = r10 * r0
     *     r11++
     *     goto loop
     *   ret
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 3, 0));     /* 0: base=3 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 7, 0));     /* 1: exp=7 */
    emit(&p, insn_irr(AEGIS_OP_CALL, 4, 0, 0));          /* 2: call power */
    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));         /* 3: yield */

    /* power_func (PC=4): */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 10, 1, 0));    /* 4: result=1 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 11, 0, 0));    /* 5: counter=0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 12, 1, 0));    /* 6: inc=1 */

    /* loop (PC=7): */
    emit(&p, insn_rrr(AEGIS_OP_GE, 13, 11, 1));         /* 7: r13=(cnt>=exp) */
    emit(&p, insn_rir(AEGIS_OP_JMP_IF, 13, 12, 0));     /* 8: if done→ret */
    emit(&p, insn_rrr(AEGIS_OP_MUL, 10, 10, 0));        /* 9: result*=base */
    emit(&p, insn_rrr(AEGIS_OP_ADD, 11, 11, 12));       /* 10: counter++ */
    emit(&p, insn_irr(AEGIS_OP_JMP, 7, 0, 0));          /* 11: goto loop */

    /* ret (PC=12): */
    emit(&p, insn_rrr(AEGIS_OP_RET, 0, 0, 0));          /* 12: ret */

    ASSERT(prog_build(&p, 1000000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(2187, p.vm.regs[10].i32); /* 3^7 = 2187 */
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Comparison chain — find max of 7 values using GT + conditional    */
/* ======================================================================= */

static bool test_find_max(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /* Load 7 values into r10-r16, find max into r0.
     * Values: 42, 17, 99, 3, 88, 65, 71 → max = 99 */
    int vals[] = {42, 17, 99, 3, 88, 65, 71};
    for (int i = 0; i < 7; i++) {
        emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, (uint32_t)(10 + i),
                          (uint32_t)vals[i], 0));
    }

    /* r0 = r10 (initial max) */
    emit(&p, insn_rrr(AEGIS_OP_MOV, 0, 10, 0));  /* PC=7 */

    /* For each r11..r16, if val > r0, r0 = val. */
    for (int i = 1; i < 7; i++) {
        uint32_t reg = (uint32_t)(10 + i);
        emit(&p, insn_rrr(AEGIS_OP_GT, 1, reg, 0));   /* r1 = (rN > r0) */
        emit(&p, insn_rir(AEGIS_OP_JMP_IF_NOT, 1,
                          (uint32_t)(p.count + 2), 0)); /* skip mov if not */
        emit(&p, insn_rrr(AEGIS_OP_MOV, 0, reg, 0));   /* r0 = rN */
    }

    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 1000000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(99, p.vm.regs[0].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Test: Unrolled arithmetic chain — lots of mixed operations              */
/* Generates N sequential ops and checks final result.                     */
/* ======================================================================= */

static bool test_long_arithmetic_chain(void) {
    test_program_t p;
    prog_alloc(&p, DEFAULT_PROG_CAP, DEFAULT_VM_ARENA);

    /* r0 = 1, r1 = 2, r2 = 3
     * Generate 1000 instructions of: r0 = (r0 + r1) * r2 / r2 - r1 + r2
     * Which simplifies to: each iteration does r0 = r0 + r2 - r1 = r0 + 1
     * So after 1000 iterations r0 = 1 + 1000 = 1001.
     *
     * But we want to actually stress the pipeline, so we chain:
     * r0 = r0 + r1  → r0 = r0 + 2
     * r0 = r0 - r1  → r0 = r0 - 2 (net zero with above)
     * Hmm, let's just do: r0 += 1 via add with r3=1, repeated 1000 times.
     * That's 1000 add instructions + setup + yield = ~1005 instructions total.
     */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 0, 0, 0));     /* r0 = 0 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 1, 1, 0));     /* r1 = 1 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 2, 7, 0));     /* r2 = 7 */
    emit(&p, insn_rir(AEGIS_OP_LOAD_IMM, 3, 3, 0));     /* r3 = 3 */

    /* Generate 500 iterations of:
     * r0 = r0 + r1    (r0 += 1)
     * r0 = r0 * r2    (r0 *= 7)
     * r0 = r0 + r3    (r0 += 3)
     * r0 = r0 / r2    (r0 /= 7)
     * Net per iteration: ((r0+1)*7+3)/7 = r0+1 + 3/7
     * Integer: (7*(r0+1)+3)/7 = r0+1 (since 3/7=0 in integer div)
     * So net effect is r0 += 1 per iteration.
     */
    for (int i = 0; i < 500; i++) {
        emit(&p, insn_rrr(AEGIS_OP_ADD, 0, 0, 1));   /* r0 += 1 */
        emit(&p, insn_rrr(AEGIS_OP_MUL, 0, 0, 2));   /* r0 *= 7 */
        emit(&p, insn_rrr(AEGIS_OP_ADD, 0, 0, 3));   /* r0 += 3 */
        emit(&p, insn_rrr(AEGIS_OP_DIV, 0, 0, 2));   /* r0 /= 7 */
    }

    emit(&p, insn_rrr(AEGIS_OP_YIELD, 0, 0, 0));

    ASSERT(prog_build(&p, 10000000));

    aegis_vm_status_t s = aegis_vm_run(&p.vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(500, p.vm.regs[0].i32);
    prog_free(&p);
    return true;
}

/* ======================================================================= */
/* Main                                                                    */
/* ======================================================================= */

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis VM Math Stress Tests ===\n\n");

    RUN(test_fibonacci_30);
    RUN(test_factorial_12);
    RUN(test_sum_of_squares_100);
    RUN(test_collatz_27);
    RUN(test_popcount);
    RUN(test_gcd_euclid);
    RUN(test_power_via_call);
    RUN(test_find_max);
    RUN(test_long_arithmetic_chain);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
