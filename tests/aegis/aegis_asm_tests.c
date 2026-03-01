/**
 * @file aegis_asm_tests.c
 * @brief Tests for Aegis IL assembler: text source → bytecode compilation.
 *
 * Tests cover: basic compilation, all operand types, labels, directives,
 * error handling, and round-trip (compile → execute → verify).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/aegis/aegis_asm.h"
#include "ferrum/aegis/aegis_bytecode.h"
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/aegis/aegis_types.h"
#include "ferrum/aegis/aegis_vm.h"

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

#define ASSERT_FLOAT_NEAR(expected, actual, eps)                             \
    do {                                                                     \
        float _e = (expected), _a = (actual);                                \
        if (fabsf(_e - _a) > (eps)) {                                        \
            printf("  ASSERT FAILED: %f != %f (eps %f, line %d)\n",          \
                   (double)_e, (double)_a, (double)(eps), __LINE__);         \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define ASSERT_STR_CONTAINS(haystack, needle)                                \
    do {                                                                     \
        if (!strstr((haystack), (needle))) {                                 \
            printf("  ASSERT FAILED: \"%s\" not in \"%s\" (line %d)\n",      \
                   (needle), (haystack), __LINE__);                          \
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
/* Helper: compile source and optionally run it                             */
/* ======================================================================= */

static bool compile(const char *src, aegis_bytecode_t *bc, aegis_asm_t *as) {
    aegis_asm_init(as);
    return aegis_asm_compile(as, src, (uint32_t)strlen(src), bc);
}

static bool compile_and_run(const char *src, aegis_vm_t *vm,
                            uint32_t fuel, uint32_t arena_size) {
    static aegis_asm_t as;
    static aegis_bytecode_t bc;
    static uint8_t arena[65536];

    aegis_asm_init(&as);
    if (!aegis_asm_compile(&as, src, (uint32_t)strlen(src), &bc)) {
        printf("  compile error (line %u): %s\n",
               aegis_asm_error_line(&as), aegis_asm_error(&as));
        return false;
    }

    if (arena_size > sizeof(arena)) arena_size = sizeof(arena);
    memset(arena, 0, arena_size);

    aegis_config_t cfg = aegis_config_default();
    cfg.fuel_budget = fuel;
    cfg.static_max  = bc.static_size > 0 ? bc.static_size : cfg.static_max;

    memset(vm, 0, sizeof(*vm));
    if (!aegis_vm_init(vm, &bc, &cfg, arena, arena_size)) {
        printf("  vm init failed\n");
        return false;
    }
    return true;
}

/* ======================================================================= */
/* Test: Trivial program — load_imm + yield                                 */
/* ======================================================================= */

static bool test_trivial_program(void) {
    const char *src =
        "load_imm r0, 42\n"
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(42, vm.regs[0].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Hex immediates                                                     */
/* ======================================================================= */

static bool test_hex_immediate(void) {
    const char *src =
        "load_imm r0, 0xFF\n"
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_U32_EQ(0xFF, vm.regs[0].u32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Negative immediates                                                */
/* ======================================================================= */

static bool test_negative_immediate(void) {
    const char *src =
        "load_imm r0, -10\n"
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(-10, vm.regs[0].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Arithmetic round-trip                                              */
/* ======================================================================= */

static bool test_arithmetic(void) {
    const char *src =
        "load_imm r0, 10\n"
        "load_imm r1, 20\n"
        "add r2, r0, r1\n"
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(30, vm.regs[2].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Comments and blank lines                                           */
/* ======================================================================= */

static bool test_comments_and_blanks(void) {
    const char *src =
        "; This is a comment\n"
        "// This too\n"
        "\n"
        "load_imm r0, 99  ; inline comment\n"
        "\n"
        "yield // done\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(99, vm.regs[0].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Labels (backward jump — loop)                                      */
/* ======================================================================= */

static bool test_backward_label(void) {
    /* Count from 0 to 5 using a backward loop. */
    const char *src =
        "load_imm r0, 0\n"       /* counter */
        "load_imm r1, 5\n"       /* limit */
        "load_imm r2, 1\n"       /* increment */
        "loop:\n"
        "add r0, r0, r2\n"
        "lt r3, r0, r1\n"
        "jmp_if r3, loop\n"
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 1000, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(5, vm.regs[0].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Forward label (jump over code)                                     */
/* ======================================================================= */

static bool test_forward_label(void) {
    const char *src =
        "load_imm r0, 1\n"
        "jmp skip\n"
        "load_imm r0, 999\n"   /* Should be skipped. */
        "skip:\n"
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(1, vm.regs[0].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: .static directive                                                  */
/* ======================================================================= */

static bool test_directive_static(void) {
    const char *src =
        ".static 128\n"
        "yield\n";

    aegis_asm_t as;
    aegis_bytecode_t bc;
    ASSERT(compile(src, &bc, &as));
    ASSERT_U32_EQ(128, bc.static_size);
    free(bc.instructions);
    return true;
}

/* ======================================================================= */
/* Test: .topic directive                                                   */
/* ======================================================================= */

static bool test_directive_topic(void) {
    const char *src =
        ".topic !hit\n"
        "yield\n";

    aegis_asm_t as;
    aegis_bytecode_t bc;
    ASSERT(compile(src, &bc, &as));
    ASSERT_U32_EQ(aegis_topic_hash("!hit"), bc.topic_hash);
    free(bc.instructions);
    return true;
}

/* ======================================================================= */
/* Test: .fuel directive                                                    */
/* ======================================================================= */

static bool test_directive_fuel(void) {
    const char *src =
        ".fuel 500\n"
        "yield\n";

    aegis_asm_t as;
    aegis_bytecode_t bc;
    ASSERT(compile(src, &bc, &as));
    /* fuel is extracted by assembler, stored in as.fuel_budget */
    ASSERT_U32_EQ(500, as.fuel_budget);
    free(bc.instructions);
    return true;
}

/* ======================================================================= */
/* Test: call/ret                                                           */
/* ======================================================================= */

static bool test_call_ret(void) {
    const char *src =
        "load_imm r0, 0\n"
        "call my_func\n"
        "yield\n"
        "my_func:\n"
        "load_imm r0, 77\n"
        "ret\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(77, vm.regs[0].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: exit instruction                                                   */
/* ======================================================================= */

static bool test_exit(void) {
    const char *src =
        "exit 42\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_EXITED, (int)s);
    ASSERT_U32_EQ(42, vm.exit_code);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: All arithmetic ops                                                 */
/* ======================================================================= */

static bool test_all_arith_ops(void) {
    const char *src =
        "load_imm r0, 10\n"
        "load_imm r1, 3\n"
        "add r2, r0, r1\n"     /* 13 */
        "sub r3, r0, r1\n"     /* 7 */
        "mul r4, r0, r1\n"     /* 30 */
        "div r5, r0, r1\n"     /* 3 */
        "mod r6, r0, r1\n"     /* 1 */
        "neg r7, r0\n"         /* -10 */
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(13, vm.regs[2].i32);
    ASSERT_INT_EQ(7, vm.regs[3].i32);
    ASSERT_INT_EQ(30, vm.regs[4].i32);
    ASSERT_INT_EQ(3, vm.regs[5].i32);
    ASSERT_INT_EQ(1, vm.regs[6].i32);
    ASSERT_INT_EQ(-10, vm.regs[7].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Bitwise ops                                                        */
/* ======================================================================= */

static bool test_bitwise_ops(void) {
    const char *src =
        "load_imm r0, 0xFF\n"
        "load_imm r1, 0x0F\n"
        "and r2, r0, r1\n"     /* 0x0F */
        "or  r3, r0, r1\n"     /* 0xFF */
        "xor r4, r0, r1\n"     /* 0xF0 */
        "not r5, r1\n"         /* ~0x0F */
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_U32_EQ(0x0F, vm.regs[2].u32);
    ASSERT_U32_EQ(0xFF, vm.regs[3].u32);
    ASSERT_U32_EQ(0xF0, vm.regs[4].u32);
    ASSERT_U32_EQ(~(uint32_t)0x0F, vm.regs[5].u32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Comparison ops                                                     */
/* ======================================================================= */

static bool test_comparison_ops(void) {
    const char *src =
        "load_imm r0, 5\n"
        "load_imm r1, 10\n"
        "eq r2, r0, r1\n"     /* 0 */
        "ne r3, r0, r1\n"     /* 1 */
        "lt r4, r0, r1\n"     /* 1 */
        "le r5, r0, r1\n"     /* 1 */
        "gt r6, r0, r1\n"     /* 0 */
        "ge r7, r0, r1\n"     /* 0 */
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(0, vm.regs[2].i32);
    ASSERT_INT_EQ(1, vm.regs[3].i32);
    ASSERT_INT_EQ(1, vm.regs[4].i32);
    ASSERT_INT_EQ(1, vm.regs[5].i32);
    ASSERT_INT_EQ(0, vm.regs[6].i32);
    ASSERT_INT_EQ(0, vm.regs[7].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Memory ops (static_store / static_load)                            */
/* ======================================================================= */

static bool test_memory_ops(void) {
    const char *src =
        ".static 64\n"
        "load_imm r0, 123\n"
        "static_store 0, r0\n"
        "load_imm r0, 0\n"      /* Clear r0. */
        "static_load r1, 0\n"
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 8192));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(123, vm.regs[1].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Error — unknown mnemonic                                           */
/* ======================================================================= */

static bool test_error_unknown_mnemonic(void) {
    const char *src =
        "frobulate r0, r1\n"
        "yield\n";

    aegis_asm_t as;
    aegis_bytecode_t bc;
    ASSERT(!compile(src, &bc, &as));
    ASSERT(as.has_error);
    ASSERT_U32_EQ(1, as.error_line);
    ASSERT_STR_CONTAINS(aegis_asm_error(&as), "frobulate");
    return true;
}

/* ======================================================================= */
/* Test: Error — invalid register                                           */
/* ======================================================================= */

static bool test_error_invalid_register(void) {
    const char *src =
        "load_imm r256, 42\n"
        "yield\n";

    aegis_asm_t as;
    aegis_bytecode_t bc;
    ASSERT(!compile(src, &bc, &as));
    ASSERT(as.has_error);
    ASSERT_U32_EQ(1, as.error_line);
    return true;
}

/* ======================================================================= */
/* Test: Error — undefined label                                            */
/* ======================================================================= */

static bool test_error_undefined_label(void) {
    const char *src =
        "jmp nowhere\n"
        "yield\n";

    aegis_asm_t as;
    aegis_bytecode_t bc;
    ASSERT(!compile(src, &bc, &as));
    ASSERT(as.has_error);
    ASSERT_STR_CONTAINS(aegis_asm_error(&as), "nowhere");
    return true;
}

/* ======================================================================= */
/* Test: Fibonacci round-trip (compile + execute + verify)                  */
/* ======================================================================= */

static bool test_fibonacci_roundtrip(void) {
    const char *src =
        "; Compute fib: 20 iterations from (0,1)\n"
        "load_imm r0, 0\n"       /* prev */
        "load_imm r1, 1\n"       /* curr */
        "load_imm r2, 0\n"       /* counter */
        "load_imm r3, 20\n"      /* limit */
        "load_imm r4, 1\n"       /* increment */
        "loop:\n"
        "mov r5, r1\n"           /* tmp = curr */
        "add r1, r1, r0\n"       /* curr = curr + prev */
        "mov r0, r5\n"           /* prev = tmp */
        "add r2, r2, r4\n"       /* counter++ */
        "lt r6, r2, r3\n"        /* counter < limit? */
        "jmp_if r6, loop\n"
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 10000, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    /* 20 iterations from (0,1) = fib(21) = 10946 */
    ASSERT_INT_EQ(10946, vm.regs[1].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Multiple labels                                                    */
/* ======================================================================= */

static bool test_multiple_labels(void) {
    const char *src =
        "load_imm r0, 0\n"
        "jmp second\n"
        "first:\n"
        "load_imm r0, 1\n"
        "jmp done\n"
        "second:\n"
        "load_imm r0, 2\n"
        "jmp first\n"
        "done:\n"
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    /* Path: 0 → jmp second → r0=2 → jmp first → r0=1 → jmp done → yield */
    ASSERT_INT_EQ(1, vm.regs[0].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: Instruction count matches                                          */
/* ======================================================================= */

static bool test_instruction_count(void) {
    const char *src =
        "load_imm r0, 1\n"
        "load_imm r1, 2\n"
        "add r2, r0, r1\n"
        "yield\n";

    aegis_asm_t as;
    aegis_bytecode_t bc;
    ASSERT(compile(src, &bc, &as));
    ASSERT_U32_EQ(4, bc.instruction_count);
    free(bc.instructions);
    return true;
}

/* ======================================================================= */
/* Test: Empty source → zero instructions                                   */
/* ======================================================================= */

static bool test_empty_source(void) {
    const char *src = "\n\n; just comments\n// more comments\n";

    aegis_asm_t as;
    aegis_bytecode_t bc;
    ASSERT(compile(src, &bc, &as));
    ASSERT_U32_EQ(0, bc.instruction_count);
    free(bc.instructions);
    return true;
}

/* ======================================================================= */
/* Test: mov instruction                                                    */
/* ======================================================================= */

static bool test_mov(void) {
    const char *src =
        "load_imm r0, 55\n"
        "mov r1, r0\n"
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(55, vm.regs[1].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Test: push/pop                                                           */
/* ======================================================================= */

static bool test_push_pop(void) {
    const char *src =
        "load_imm r0, 42\n"
        "push r0\n"
        "load_imm r0, 0\n"
        "pop r1\n"
        "yield\n";

    aegis_vm_t vm;
    ASSERT(compile_and_run(src, &vm, 100, 4096));
    aegis_vm_status_t s = aegis_vm_run(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    ASSERT_INT_EQ(42, vm.regs[1].i32);
    free(vm.bytecode->instructions);
    return true;
}

/* ======================================================================= */
/* Main                                                                     */
/* ======================================================================= */

int main(void) {
    printf("=== Aegis IL Assembler Tests ===\n\n");

    /* Basic compilation */
    RUN(test_trivial_program);
    RUN(test_hex_immediate);
    RUN(test_negative_immediate);
    RUN(test_arithmetic);
    RUN(test_comments_and_blanks);
    RUN(test_mov);
    RUN(test_push_pop);
    RUN(test_exit);

    /* Labels */
    RUN(test_backward_label);
    RUN(test_forward_label);
    RUN(test_multiple_labels);
    RUN(test_call_ret);

    /* Directives */
    RUN(test_directive_static);
    RUN(test_directive_topic);
    RUN(test_directive_fuel);

    /* Operations */
    RUN(test_all_arith_ops);
    RUN(test_bitwise_ops);
    RUN(test_comparison_ops);
    RUN(test_memory_ops);

    /* Round-trip */
    RUN(test_fibonacci_roundtrip);

    /* Meta */
    RUN(test_instruction_count);
    RUN(test_empty_source);

    /* Error cases */
    RUN(test_error_unknown_mnemonic);
    RUN(test_error_invalid_register);
    RUN(test_error_undefined_label);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
