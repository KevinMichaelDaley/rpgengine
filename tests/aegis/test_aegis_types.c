/**
 * @file test_aegis_types.c
 * @brief Unit tests for Aegis VM core types and instruction encoding.
 *
 * Covers: register union layout, instruction encoding, opcode enum,
 * immediate flag extraction, bytecode container, configuration defaults.
 *
 * Per ref/aegis_bytecode_spec.md §3.1, §3.2, §3.4.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/aegis/aegis_types.h"
#include "ferrum/aegis/aegis_bytecode.h"
#include "ferrum/aegis/aegis_config.h"

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

/* ======================================================================= */
/* Register union layout tests (§3.1)                                       */
/* ======================================================================= */

/** Register is exactly 16 bytes. */
static bool test_register_size(void) {
    ASSERT_INT_EQ(16, (int)sizeof(aegis_register_t));
    return true;
}

/** Writing i32 and reading it back. */
static bool test_register_i32(void) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.i32 = -42;
    ASSERT_INT_EQ(-42, r.i32);
    return true;
}

/** Writing i64 and reading it back. */
static bool test_register_i64(void) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.i64 = (int64_t)0x7FFFFFFFFFFFFFFFLL;
    ASSERT(r.i64 == (int64_t)0x7FFFFFFFFFFFFFFFLL);
    return true;
}

/** Writing f32 and reading it back. */
static bool test_register_f32(void) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.f32 = 3.14f;
    ASSERT_FLOAT_EQ(3.14f, r.f32);
    return true;
}

/** Writing f64 and reading it back. */
static bool test_register_f64(void) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.f64 = 2.718281828;
    ASSERT(fabs(r.f64 - 2.718281828) < 1e-9);
    return true;
}

/** Vec2 occupies first 8 bytes. */
static bool test_register_vec2(void) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.vec2[0] = 1.0f;
    r.vec2[1] = 2.0f;
    ASSERT_FLOAT_EQ(1.0f, r.vec2[0]);
    ASSERT_FLOAT_EQ(2.0f, r.vec2[1]);
    return true;
}

/** Vec3 occupies first 12 bytes. */
static bool test_register_vec3(void) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.vec3[0] = 10.0f;
    r.vec3[1] = 20.0f;
    r.vec3[2] = 30.0f;
    ASSERT_FLOAT_EQ(10.0f, r.vec3[0]);
    ASSERT_FLOAT_EQ(20.0f, r.vec3[1]);
    ASSERT_FLOAT_EQ(30.0f, r.vec3[2]);
    return true;
}

/** Vec4/quat occupies all 16 bytes. */
static bool test_register_vec4_quat(void) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.vec4[0] = 0.0f;
    r.vec4[1] = 0.0f;
    r.vec4[2] = 0.0f;
    r.vec4[3] = 1.0f;
    ASSERT_FLOAT_EQ(1.0f, r.vec4[3]);
    return true;
}

/** entity_id alias maps to u32. */
static bool test_register_entity_id(void) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    r.entity_id = 12345;
    ASSERT_INT_EQ(12345, (int)r.entity_id);
    /* entity_id and u32 share the same union position. */
    ASSERT_INT_EQ(12345, (int)r.u32);
    return true;
}

/** Bytes array fills entire register. */
static bool test_register_bytes(void) {
    aegis_register_t r;
    memset(&r, 0xFF, sizeof(r));
    memset(r.bytes, 0xAB, 16);
    for (int i = 0; i < 16; i++) {
        ASSERT_INT_EQ(0xAB, r.bytes[i]);
    }
    return true;
}

/* ======================================================================= */
/* Instruction encoding tests (§3.4)                                        */
/* ======================================================================= */

/** Instruction is exactly 16 bytes. */
static bool test_instruction_size(void) {
    ASSERT_INT_EQ(16, (int)sizeof(aegis_instruction_t));
    return true;
}

/** Encode/decode opcode from word 0. */
static bool test_instruction_opcode_encode(void) {
    aegis_instruction_t insn;
    memset(&insn, 0, sizeof(insn));
    insn.words[0] = (uint32_t)AEGIS_OP_ADD;
    ASSERT_INT_EQ(AEGIS_OP_ADD, (int)aegis_insn_opcode(&insn));
    return true;
}

/** Immediate flag bit 16 (operand A). */
static bool test_imm_flag_a(void) {
    aegis_instruction_t insn;
    memset(&insn, 0, sizeof(insn));
    insn.words[0] = (uint32_t)AEGIS_OP_LOAD_IMM | AEGIS_IMM_A;
    ASSERT(aegis_insn_imm_a(&insn));
    ASSERT(!aegis_insn_imm_b(&insn));
    ASSERT(!aegis_insn_imm_c(&insn));
    return true;
}

/** Immediate flag bit 17 (operand B). */
static bool test_imm_flag_b(void) {
    aegis_instruction_t insn;
    memset(&insn, 0, sizeof(insn));
    insn.words[0] = (uint32_t)AEGIS_OP_LOAD_IMM | AEGIS_IMM_B;
    ASSERT(!aegis_insn_imm_a(&insn));
    ASSERT(aegis_insn_imm_b(&insn));
    ASSERT(!aegis_insn_imm_c(&insn));
    return true;
}

/** Immediate flag bit 18 (operand C). */
static bool test_imm_flag_c(void) {
    aegis_instruction_t insn;
    memset(&insn, 0, sizeof(insn));
    insn.words[0] = (uint32_t)AEGIS_OP_LOAD_IMM | AEGIS_IMM_C;
    ASSERT(!aegis_insn_imm_a(&insn));
    ASSERT(!aegis_insn_imm_b(&insn));
    ASSERT(aegis_insn_imm_c(&insn));
    return true;
}

/** All three immediate flags set simultaneously. */
static bool test_imm_flags_all(void) {
    aegis_instruction_t insn;
    memset(&insn, 0, sizeof(insn));
    insn.words[0] = (uint32_t)AEGIS_OP_ADD | AEGIS_IMM_A | AEGIS_IMM_B | AEGIS_IMM_C;
    ASSERT(aegis_insn_imm_a(&insn));
    ASSERT(aegis_insn_imm_b(&insn));
    ASSERT(aegis_insn_imm_c(&insn));
    return true;
}

/** Operand extraction from words 1-3. */
static bool test_instruction_operands(void) {
    aegis_instruction_t insn;
    insn.words[0] = (uint32_t)AEGIS_OP_VEC3_ADD;
    insn.words[1] = 2;   /* r_dst = r2 */
    insn.words[2] = 0;   /* r_a = r0 */
    insn.words[3] = 1;   /* r_b = r1 */
    ASSERT_INT_EQ(2, (int)aegis_insn_a(&insn));
    ASSERT_INT_EQ(0, (int)aegis_insn_b(&insn));
    ASSERT_INT_EQ(1, (int)aegis_insn_c(&insn));
    return true;
}

/** Spec example: vec3_add r2, r0, r1 encoding. */
static bool test_spec_example_vec3_add(void) {
    aegis_instruction_t insn;
    insn.words[0] = (uint32_t)AEGIS_OP_VEC3_ADD; /* no immediate flags */
    insn.words[1] = 2;
    insn.words[2] = 0;
    insn.words[3] = 1;
    ASSERT_INT_EQ(AEGIS_OP_VEC3_ADD, (int)aegis_insn_opcode(&insn));
    ASSERT(!aegis_insn_imm_a(&insn));
    ASSERT(!aegis_insn_imm_b(&insn));
    ASSERT(!aegis_insn_imm_c(&insn));
    ASSERT_INT_EQ(2, (int)aegis_insn_a(&insn));
    ASSERT_INT_EQ(0, (int)aegis_insn_b(&insn));
    ASSERT_INT_EQ(1, (int)aegis_insn_c(&insn));
    return true;
}

/** Spec example: load_imm r5, 0.1f encoding (bit 17 set). */
static bool test_spec_example_load_imm(void) {
    aegis_instruction_t insn;
    insn.words[0] = (uint32_t)AEGIS_OP_LOAD_IMM | AEGIS_IMM_B;
    insn.words[1] = 5;  /* r5 */
    /* 0.1f as IEEE 754 */
    float val = 0.1f;
    memcpy(&insn.words[2], &val, sizeof(float));
    insn.words[3] = 0;

    ASSERT_INT_EQ(AEGIS_OP_LOAD_IMM, (int)aegis_insn_opcode(&insn));
    ASSERT(!aegis_insn_imm_a(&insn));
    ASSERT(aegis_insn_imm_b(&insn));
    ASSERT_INT_EQ(5, (int)aegis_insn_a(&insn));
    /* Verify the float round-trips. */
    float out;
    memcpy(&out, &insn.words[2], sizeof(float));
    ASSERT_FLOAT_EQ(0.1f, out);
    return true;
}

/* ======================================================================= */
/* Opcode enum coverage tests (§3.3)                                        */
/* ======================================================================= */

/** Opcode values are within 16-bit range. */
static bool test_opcode_range(void) {
    ASSERT((int)AEGIS_OP_COUNT <= 65536);
    /* First opcode is 0. */
    ASSERT_INT_EQ(0, (int)AEGIS_OP_YIELD);
    return true;
}

/** Coroutine opcodes exist and are distinct. */
static bool test_opcodes_coroutine(void) {
    ASSERT((int)AEGIS_OP_YIELD != (int)AEGIS_OP_RESUME);
    ASSERT((int)AEGIS_OP_RESUME != (int)AEGIS_OP_EXIT);
    ASSERT((int)AEGIS_OP_YIELD != (int)AEGIS_OP_EXIT);
    return true;
}

/** Arithmetic opcodes exist. */
static bool test_opcodes_arithmetic(void) {
    int ops[] = {
        AEGIS_OP_ADD, AEGIS_OP_SUB, AEGIS_OP_MUL, AEGIS_OP_DIV,
        AEGIS_OP_MOD, AEGIS_OP_NEG
    };
    /* All distinct. */
    for (int i = 0; i < 6; i++) {
        for (int j = i + 1; j < 6; j++) {
            ASSERT(ops[i] != ops[j]);
        }
    }
    return true;
}

/** Comparison opcodes exist. */
static bool test_opcodes_comparison(void) {
    int ops[] = {
        AEGIS_OP_EQ, AEGIS_OP_NE, AEGIS_OP_LT, AEGIS_OP_LE,
        AEGIS_OP_GT, AEGIS_OP_GE
    };
    for (int i = 0; i < 6; i++) {
        for (int j = i + 1; j < 6; j++) {
            ASSERT(ops[i] != ops[j]);
        }
    }
    return true;
}

/** Control flow opcodes exist. */
static bool test_opcodes_control_flow(void) {
    int ops[] = {
        AEGIS_OP_JMP, AEGIS_OP_JMP_IF, AEGIS_OP_JMP_IF_NOT,
        AEGIS_OP_CALL, AEGIS_OP_RET
    };
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            ASSERT(ops[i] != ops[j]);
        }
    }
    return true;
}

/** Data movement opcodes exist. */
static bool test_opcodes_data_movement(void) {
    int ops[] = {
        AEGIS_OP_MOV, AEGIS_OP_LOAD_IMM, AEGIS_OP_LOAD_IMM64
    };
    for (int i = 0; i < 3; i++) {
        for (int j = i + 1; j < 3; j++) {
            ASSERT(ops[i] != ops[j]);
        }
    }
    return true;
}

/** Memory opcodes exist. */
static bool test_opcodes_memory(void) {
    int ops[] = {
        AEGIS_OP_ALLOC, AEGIS_OP_LOAD, AEGIS_OP_STORE,
        AEGIS_OP_STATIC_LOAD, AEGIS_OP_STATIC_STORE,
        AEGIS_OP_PUSH, AEGIS_OP_POP
    };
    for (int i = 0; i < 7; i++) {
        for (int j = i + 1; j < 7; j++) {
            ASSERT(ops[i] != ops[j]);
        }
    }
    return true;
}

/** Vector/quat opcodes exist. */
static bool test_opcodes_vector(void) {
    int ops[] = {
        AEGIS_OP_VEC3_ADD, AEGIS_OP_VEC3_SUB, AEGIS_OP_VEC3_MUL,
        AEGIS_OP_VEC3_SCALE, AEGIS_OP_VEC3_DOT, AEGIS_OP_VEC3_CROSS,
        AEGIS_OP_VEC3_LEN, AEGIS_OP_VEC3_NORM,
        AEGIS_OP_QUAT_MUL, AEGIS_OP_QUAT_ROTATE
    };
    for (int i = 0; i < 10; i++) {
        for (int j = i + 1; j < 10; j++) {
            ASSERT(ops[i] != ops[j]);
        }
    }
    return true;
}

/** Entity/event opcodes exist. */
static bool test_opcodes_entity_event(void) {
    int ops[] = {
        AEGIS_OP_EVENT_TYPE, AEGIS_OP_EVENT_SRC, AEGIS_OP_EVENT_FIELD,
        AEGIS_OP_QUERY_ENTITY, AEGIS_OP_GET_ATTR,
        AEGIS_OP_ENTITY_COUNT, AEGIS_OP_ENTITY_AT,
        AEGIS_OP_BUILD_UPDATE, AEGIS_OP_TARGET_ENTITY,
        AEGIS_OP_SET_FIELD, AEGIS_OP_ADD_HINT, AEGIS_OP_PUSH_UPDATE
    };
    for (int i = 0; i < 12; i++) {
        for (int j = i + 1; j < 12; j++) {
            ASSERT(ops[i] != ops[j]);
        }
    }
    return true;
}

/** Async opcodes exist. */
static bool test_opcodes_async(void) {
    int ops[] = {
        AEGIS_OP_WAIT, AEGIS_OP_POLL,
        AEGIS_OP_VIS_TEST, AEGIS_OP_NAV_QUERY
    };
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            ASSERT(ops[i] != ops[j]);
        }
    }
    return true;
}

/** Type conversion opcodes exist. */
static bool test_opcodes_conversion(void) {
    int ops[] = {
        AEGIS_OP_I32_TO_F32, AEGIS_OP_F32_TO_I32,
        AEGIS_OP_I64_TO_F64, AEGIS_OP_F64_TO_I64,
        AEGIS_OP_F64_TO_F32, AEGIS_OP_F32_TO_F64
    };
    for (int i = 0; i < 6; i++) {
        for (int j = i + 1; j < 6; j++) {
            ASSERT(ops[i] != ops[j]);
        }
    }
    return true;
}

/** Bitwise/logic opcodes exist. */
static bool test_opcodes_bitwise(void) {
    int ops[] = {
        AEGIS_OP_AND, AEGIS_OP_OR, AEGIS_OP_XOR, AEGIS_OP_NOT
    };
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            ASSERT(ops[i] != ops[j]);
        }
    }
    return true;
}

/* ======================================================================= */
/* Instruction builder helper tests                                         */
/* ======================================================================= */

/** aegis_insn_make encodes correctly. */
static bool test_insn_make(void) {
    aegis_instruction_t insn = aegis_insn_make(AEGIS_OP_ADD, 0, 3, 10, 20);
    ASSERT_INT_EQ(AEGIS_OP_ADD, (int)aegis_insn_opcode(&insn));
    ASSERT(!aegis_insn_imm_a(&insn));
    ASSERT(!aegis_insn_imm_b(&insn));
    ASSERT(!aegis_insn_imm_c(&insn));
    ASSERT_INT_EQ(3, (int)aegis_insn_a(&insn));
    ASSERT_INT_EQ(10, (int)aegis_insn_b(&insn));
    ASSERT_INT_EQ(20, (int)aegis_insn_c(&insn));
    return true;
}

/** aegis_insn_make with immediate flags. */
static bool test_insn_make_imm(void) {
    aegis_instruction_t insn = aegis_insn_make(
        AEGIS_OP_LOAD_IMM, AEGIS_IMM_B, 5, 0x3DCCCCCD, 0);
    ASSERT_INT_EQ(AEGIS_OP_LOAD_IMM, (int)aegis_insn_opcode(&insn));
    ASSERT(!aegis_insn_imm_a(&insn));
    ASSERT(aegis_insn_imm_b(&insn));
    ASSERT(!aegis_insn_imm_c(&insn));
    ASSERT_INT_EQ(5, (int)aegis_insn_a(&insn));
    return true;
}

/* ======================================================================= */
/* Bytecode container tests                                                 */
/* ======================================================================= */

/** Bytecode init sets sensible defaults. */
static bool test_bytecode_init(void) {
    aegis_bytecode_t bc;
    aegis_bytecode_init(&bc);
    ASSERT(bc.instructions == NULL);
    ASSERT_INT_EQ(0, (int)bc.instruction_count);
    ASSERT_INT_EQ(0, (int)bc.constant_count);
    ASSERT_INT_EQ(0, (int)bc.static_size);
    ASSERT_INT_EQ(0, (int)bc.topic_hash);
    return true;
}

/* ======================================================================= */
/* Config tests                                                             */
/* ======================================================================= */

/** Default config has sane values. */
static bool test_config_defaults(void) {
    aegis_config_t cfg = aegis_config_default();
    ASSERT(cfg.arena_size > 0);
    ASSERT(cfg.static_max > 0);
    ASSERT(cfg.stack_max > 0);
    ASSERT(cfg.fuel_budget > 0);
    ASSERT(cfg.wall_time_budget_ns > 0);
    ASSERT(cfg.max_updates > 0);
    ASSERT(cfg.max_async_tasks > 0);
    return true;
}

/** Config arena_size must be larger than static_max + stack_max. */
static bool test_config_arena_sizing(void) {
    aegis_config_t cfg = aegis_config_default();
    ASSERT(cfg.arena_size >= cfg.static_max + cfg.stack_max);
    return true;
}

/* ======================================================================= */
/* Edge case / failure tests                                                */
/* ======================================================================= */

/** Zero-initialized register reads back as zero for all fields. */
static bool test_register_zero_init(void) {
    aegis_register_t r;
    memset(&r, 0, sizeof(r));
    ASSERT_INT_EQ(0, r.i32);
    ASSERT(r.i64 == 0);
    ASSERT_FLOAT_EQ(0.0f, r.f32);
    ASSERT(r.f64 == 0.0);
    ASSERT_INT_EQ(0, (int)r.u32);
    ASSERT(r.u64 == 0);
    ASSERT_INT_EQ(0, (int)r.entity_id);
    return true;
}

/** Opcode does not bleed into immediate flags. */
static bool test_opcode_no_flag_bleed(void) {
    /* Use an opcode value that has high bits in the 16-bit range. */
    aegis_instruction_t insn;
    memset(&insn, 0, sizeof(insn));
    insn.words[0] = 0x0000FFFF; /* max 16-bit opcode, no imm flags */
    ASSERT_INT_EQ(0xFFFF, (int)aegis_insn_opcode(&insn));
    ASSERT(!aegis_insn_imm_a(&insn));
    ASSERT(!aegis_insn_imm_b(&insn));
    ASSERT(!aegis_insn_imm_c(&insn));
    return true;
}

/** Immediate flags do not interfere with opcode extraction. */
static bool test_flags_no_opcode_bleed(void) {
    aegis_instruction_t insn;
    memset(&insn, 0, sizeof(insn));
    insn.words[0] = (uint32_t)AEGIS_OP_ADD | AEGIS_IMM_A | AEGIS_IMM_B | AEGIS_IMM_C;
    /* Opcode should still be ADD, not polluted by flags. */
    ASSERT_INT_EQ(AEGIS_OP_ADD, (int)aegis_insn_opcode(&insn));
    return true;
}

/** Reserved bits (19-31) are masked out of opcode. */
static bool test_reserved_bits_masked(void) {
    aegis_instruction_t insn;
    memset(&insn, 0, sizeof(insn));
    /* Set reserved bits — opcode extraction must ignore them. */
    insn.words[0] = (uint32_t)AEGIS_OP_MOV | 0xFFF80000u;
    ASSERT_INT_EQ(AEGIS_OP_MOV, (int)aegis_insn_opcode(&insn));
    return true;
}

/** Max register index fits in uint32 operand. */
static bool test_max_register_index(void) {
    aegis_instruction_t insn = aegis_insn_make(AEGIS_OP_MOV, 0, 255, 255, 0);
    ASSERT_INT_EQ(255, (int)aegis_insn_a(&insn));
    ASSERT_INT_EQ(255, (int)aegis_insn_b(&insn));
    return true;
}

/* ======================================================================= */
/* Main                                                                     */
/* ======================================================================= */

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis Types Tests ===\n\n");

    /* Register layout */
    RUN(test_register_size);
    RUN(test_register_i32);
    RUN(test_register_i64);
    RUN(test_register_f32);
    RUN(test_register_f64);
    RUN(test_register_vec2);
    RUN(test_register_vec3);
    RUN(test_register_vec4_quat);
    RUN(test_register_entity_id);
    RUN(test_register_bytes);
    RUN(test_register_zero_init);

    /* Instruction encoding */
    RUN(test_instruction_size);
    RUN(test_instruction_opcode_encode);
    RUN(test_imm_flag_a);
    RUN(test_imm_flag_b);
    RUN(test_imm_flag_c);
    RUN(test_imm_flags_all);
    RUN(test_instruction_operands);
    RUN(test_spec_example_vec3_add);
    RUN(test_spec_example_load_imm);

    /* Instruction builder */
    RUN(test_insn_make);
    RUN(test_insn_make_imm);

    /* Opcode coverage */
    RUN(test_opcode_range);
    RUN(test_opcodes_coroutine);
    RUN(test_opcodes_arithmetic);
    RUN(test_opcodes_comparison);
    RUN(test_opcodes_control_flow);
    RUN(test_opcodes_data_movement);
    RUN(test_opcodes_memory);
    RUN(test_opcodes_vector);
    RUN(test_opcodes_entity_event);
    RUN(test_opcodes_async);
    RUN(test_opcodes_conversion);
    RUN(test_opcodes_bitwise);

    /* Bytecode container */
    RUN(test_bytecode_init);

    /* Config */
    RUN(test_config_defaults);
    RUN(test_config_arena_sizing);

    /* Edge cases */
    RUN(test_opcode_no_flag_bleed);
    RUN(test_flags_no_opcode_bleed);
    RUN(test_reserved_bits_masked);
    RUN(test_max_register_index);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
