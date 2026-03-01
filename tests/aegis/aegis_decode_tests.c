/**
 * @file aegis_decode_tests.c
 * @brief Unit tests for Aegis instruction decoder.
 *
 * Covers: opcode extraction, immediate flag handling, register read,
 * register index validation, boundary indices, mixed modes.
 *
 * Per ref/aegis_bytecode_spec.md §3.4.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/aegis/aegis_decode.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

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

#define ASSERT_FLOAT_EQ(a, b) do { \
    if (fabsf((a) - (b)) > 1e-6f) { \
        printf("  ASSERT FAILED: %f != %f (line %d)\n", \
               (double)(a), (double)(b), __LINE__); \
        return false; \
    } \
} while (0)

/* ----------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ----------------------------------------------------------------------- */

/** Make a register file with known values. */
static void init_regs(aegis_register_t regs[AEGIS_REGISTER_COUNT]) {
    memset(regs, 0, sizeof(aegis_register_t) * AEGIS_REGISTER_COUNT);
    regs[0].i32 = 100;
    regs[1].i32 = 200;
    regs[2].i32 = 300;
    regs[10].f32 = 1.5f;
    regs[255].i32 = -1;
}

/* ======================================================================= */
/* All-register instruction                                                 */
/* ======================================================================= */

/** Decode add r2, r0, r1 — all operands are registers. */
static bool test_decode_all_register(void) {
    aegis_register_t regs[AEGIS_REGISTER_COUNT];
    init_regs(regs);

    aegis_instruction_t insn = aegis_insn_make(AEGIS_OP_ADD, 0, 2, 0, 1);
    aegis_decode_result_t result;
    ASSERT(aegis_decode(&insn, regs, &result));

    ASSERT_INT_EQ(AEGIS_OP_ADD, (int)result.opcode);
    /* A = r2 → value 300 */
    ASSERT_INT_EQ(300, result.a.i32);
    /* B = r0 → value 100 */
    ASSERT_INT_EQ(100, result.b.i32);
    /* C = r1 → value 200 */
    ASSERT_INT_EQ(200, result.c.i32);
    return true;
}

/* ======================================================================= */
/* All-immediate instruction                                                */
/* ======================================================================= */

/** Decode with all three operands as immediates. */
static bool test_decode_all_immediate(void) {
    aegis_register_t regs[AEGIS_REGISTER_COUNT];
    init_regs(regs);

    aegis_instruction_t insn = aegis_insn_make(
        AEGIS_OP_ADD,
        AEGIS_IMM_A | AEGIS_IMM_B | AEGIS_IMM_C,
        42, 100, 200);
    aegis_decode_result_t result;
    ASSERT(aegis_decode(&insn, regs, &result));

    ASSERT_INT_EQ(AEGIS_OP_ADD, (int)result.opcode);
    /* Immediates are raw uint32 values. */
    ASSERT_INT_EQ(42, (int)result.a.u32);
    ASSERT_INT_EQ(100, (int)result.b.u32);
    ASSERT_INT_EQ(200, (int)result.c.u32);
    return true;
}

/* ======================================================================= */
/* Mixed register/immediate                                                  */
/* ======================================================================= */

/** load_imm r5, 0.1f — A=register, B=immediate. */
static bool test_decode_mixed(void) {
    aegis_register_t regs[AEGIS_REGISTER_COUNT];
    init_regs(regs);
    regs[5].i32 = 555;

    float imm_val = 0.1f;
    uint32_t imm_bits;
    memcpy(&imm_bits, &imm_val, sizeof(uint32_t));

    aegis_instruction_t insn = aegis_insn_make(
        AEGIS_OP_LOAD_IMM, AEGIS_IMM_B, 5, imm_bits, 0);
    aegis_decode_result_t result;
    ASSERT(aegis_decode(&insn, regs, &result));

    ASSERT_INT_EQ(AEGIS_OP_LOAD_IMM, (int)result.opcode);
    /* A is register mode → reads r5. */
    ASSERT_INT_EQ(555, result.a.i32);
    /* B is immediate → raw bits. */
    ASSERT_FLOAT_EQ(0.1f, result.b.f32);
    return true;
}

/* ======================================================================= */
/* Zero-operand instruction                                                  */
/* ======================================================================= */

/** yield — no meaningful operands. */
static bool test_decode_zero_operands(void) {
    aegis_register_t regs[AEGIS_REGISTER_COUNT];
    init_regs(regs);

    aegis_instruction_t insn = aegis_insn_make(AEGIS_OP_YIELD, 0, 0, 0, 0);
    aegis_decode_result_t result;
    ASSERT(aegis_decode(&insn, regs, &result));
    ASSERT_INT_EQ(AEGIS_OP_YIELD, (int)result.opcode);
    return true;
}

/* ======================================================================= */
/* Boundary register indices                                                 */
/* ======================================================================= */

/** Register index 0 decodes correctly. */
static bool test_decode_reg_index_0(void) {
    aegis_register_t regs[AEGIS_REGISTER_COUNT];
    init_regs(regs);

    aegis_instruction_t insn = aegis_insn_make(AEGIS_OP_MOV, 0, 0, 0, 0);
    aegis_decode_result_t result;
    ASSERT(aegis_decode(&insn, regs, &result));
    ASSERT_INT_EQ(100, result.a.i32);
    return true;
}

/** Register index 255 decodes correctly. */
static bool test_decode_reg_index_255(void) {
    aegis_register_t regs[AEGIS_REGISTER_COUNT];
    init_regs(regs);

    aegis_instruction_t insn = aegis_insn_make(AEGIS_OP_MOV, 0, 255, 255, 0);
    aegis_decode_result_t result;
    ASSERT(aegis_decode(&insn, regs, &result));
    ASSERT_INT_EQ(-1, result.a.i32);
    ASSERT_INT_EQ(-1, result.b.i32);
    return true;
}

/** Register index 256 is invalid → decode error. */
static bool test_decode_invalid_reg_a(void) {
    aegis_register_t regs[AEGIS_REGISTER_COUNT];
    init_regs(regs);

    aegis_instruction_t insn = aegis_insn_make(AEGIS_OP_MOV, 0, 256, 0, 0);
    aegis_decode_result_t result;
    ASSERT(!aegis_decode(&insn, regs, &result));
    return true;
}

/** Invalid register index in B position. */
static bool test_decode_invalid_reg_b(void) {
    aegis_register_t regs[AEGIS_REGISTER_COUNT];
    init_regs(regs);

    aegis_instruction_t insn = aegis_insn_make(AEGIS_OP_ADD, 0, 0, 300, 0);
    aegis_decode_result_t result;
    ASSERT(!aegis_decode(&insn, regs, &result));
    return true;
}

/** Invalid register index in C position. */
static bool test_decode_invalid_reg_c(void) {
    aegis_register_t regs[AEGIS_REGISTER_COUNT];
    init_regs(regs);

    aegis_instruction_t insn = aegis_insn_make(AEGIS_OP_ADD, 0, 0, 0, 1000);
    aegis_decode_result_t result;
    ASSERT(!aegis_decode(&insn, regs, &result));
    return true;
}

/** Immediate flag set bypasses register validation (>255 is OK as immediate). */
static bool test_decode_imm_bypasses_validation(void) {
    aegis_register_t regs[AEGIS_REGISTER_COUNT];
    init_regs(regs);

    aegis_instruction_t insn = aegis_insn_make(
        AEGIS_OP_ADD, AEGIS_IMM_A, 999, 0, 0);
    aegis_decode_result_t result;
    ASSERT(aegis_decode(&insn, regs, &result));
    ASSERT_INT_EQ(999, (int)result.a.u32);
    return true;
}

/* ======================================================================= */
/* Decode preserves raw operand indices                                      */
/* ======================================================================= */

/** Raw operand indices are preserved in result. */
static bool test_decode_raw_operands(void) {
    aegis_register_t regs[AEGIS_REGISTER_COUNT];
    init_regs(regs);

    aegis_instruction_t insn = aegis_insn_make(AEGIS_OP_ADD, 0, 2, 0, 1);
    aegis_decode_result_t result;
    ASSERT(aegis_decode(&insn, regs, &result));

    ASSERT_INT_EQ(2, (int)result.raw_a);
    ASSERT_INT_EQ(0, (int)result.raw_b);
    ASSERT_INT_EQ(1, (int)result.raw_c);
    return true;
}

/* ======================================================================= */
/* Main                                                                     */
/* ======================================================================= */

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis Decoder Tests ===\n\n");

    RUN(test_decode_all_register);
    RUN(test_decode_all_immediate);
    RUN(test_decode_mixed);
    RUN(test_decode_zero_operands);
    RUN(test_decode_reg_index_0);
    RUN(test_decode_reg_index_255);
    RUN(test_decode_invalid_reg_a);
    RUN(test_decode_invalid_reg_b);
    RUN(test_decode_invalid_reg_c);
    RUN(test_decode_imm_bypasses_validation);
    RUN(test_decode_raw_operands);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
