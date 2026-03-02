/**
 * @file aegis_types.h
 * @brief Core Aegis VM types: register union, instruction encoding, opcode enum.
 *
 * Per ref/aegis_bytecode_spec.md §3.1, §3.2, §3.3, §3.4.
 *
 * All instructions are 128-bit fixed-width (4 × uint32_t).
 * Registers are 128-bit unions supporting i32/i64/f32/f64/vec2/vec3/vec4/entity_id/bytes.
 */

#ifndef FERRUM_AEGIS_TYPES_H
#define FERRUM_AEGIS_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================= */
/* Register (§3.1)                                                          */
/* ======================================================================= */

/**
 * @brief 128-bit general-purpose register.
 *
 * 256 registers × 16 bytes = 4 KB register file (fits in L1 cache).
 * Ownership: the VM owns the register file; scripts read/write via instructions.
 * Nullability: register contents are always valid (zero-initialized on reset).
 */
typedef union aegis_register {
    int32_t   i32;
    int64_t   i64;
    float     f32;
    double    f64;
    uint32_t  u32;
    uint64_t  u64;
    float     vec2[2];
    float     vec3[3];
    float     vec4[4];       /**< Also used for quaternions. */
    uint32_t  entity_id;
    uint32_t  handle;        /**< Async handle, entity query handle. */
    uint8_t   bytes[16];
} aegis_register_t;

_Static_assert(sizeof(aegis_register_t) == 16, "register must be 16 bytes");

/** Number of general-purpose registers in the VM. */
#define AEGIS_REGISTER_COUNT 256

/** Size of the register file in bytes. */
#define AEGIS_REGISTER_FILE_SIZE (AEGIS_REGISTER_COUNT * sizeof(aegis_register_t))

/* ======================================================================= */
/* Opcode enum (§3.3)                                                       */
/* ======================================================================= */

/**
 * @brief Bytecode opcodes for all Aegis VM instructions.
 *
 * Opcodes occupy bits 0-15 of instruction word 0 (max 65536 values).
 * All mnemonics are snake_case per engine convention.
 * Grouped by category matching spec §3.3.
 */
typedef enum aegis_opcode {
    /* -- Coroutine control -- */
    AEGIS_OP_YIELD          = 0x00,  /**< Serialize state, return update_set. */
    AEGIS_OP_RESUME         = 0x01,  /**< Entry point marker. */
    AEGIS_OP_EXIT           = 0x02,  /**< Terminate script with error code. */

    /* -- Function calls -- */
    AEGIS_OP_CALL           = 0x03,  /**< Push frame, jump to label. */
    AEGIS_OP_RET            = 0x04,  /**< Pop frame, return to caller. */

    /* -- Async operations -- */
    AEGIS_OP_WAIT           = 0x05,  /**< Poll + yield fiber if pending. */
    AEGIS_OP_POLL           = 0x06,  /**< Read async task status (non-blocking). */

    /* -- Event access -- */
    AEGIS_OP_EVENT_TYPE     = 0x07,  /**< Load event type hash. */
    AEGIS_OP_EVENT_SRC      = 0x08,  /**< Load source entity ID. */
    AEGIS_OP_EVENT_FIELD    = 0x09,  /**< Load typed field from event payload. */

    /* -- World queries -- */
    AEGIS_OP_QUERY_ENTITY   = 0x0A,  /**< Find entity in snapshot by ID. */
    AEGIS_OP_GET_ATTR       = 0x0B,  /**< Read attribute from entity snapshot. */
    AEGIS_OP_ENTITY_COUNT   = 0x0C,  /**< Number of active entities. */
    AEGIS_OP_ENTITY_AT      = 0x0D,  /**< Entity handle at snapshot index. */

    /* -- Async world queries -- */
    AEGIS_OP_VIS_TEST       = 0x0E,  /**< Submit async raycast. */
    AEGIS_OP_NAV_QUERY      = 0x0F,  /**< Submit async nav mesh query. */

    /* -- State mutation -- */
    AEGIS_OP_BUILD_UPDATE   = 0x10,  /**< Create empty update builder. */
    AEGIS_OP_TARGET_ENTITY  = 0x11,  /**< Set target entity for update. */
    AEGIS_OP_SET_FIELD      = 0x12,  /**< Set attribute value in update. */
    AEGIS_OP_ADD_HINT       = 0x13,  /**< Add validation hint flag. */
    AEGIS_OP_PUSH_UPDATE    = 0x14,  /**< Finalize and append to update_set. */

    /* -- Data movement -- */
    AEGIS_OP_MOV            = 0x15,  /**< Copy register. */
    AEGIS_OP_LOAD_IMM       = 0x16,  /**< Load 32-bit immediate. */
    AEGIS_OP_LOAD_IMM64     = 0x17,  /**< Load 64-bit immediate (lo + hi). */

    /* -- Arithmetic -- */
    AEGIS_OP_ADD            = 0x18,  /**< r_dst = r_a + r_b. */
    AEGIS_OP_SUB            = 0x19,  /**< r_dst = r_a - r_b. */
    AEGIS_OP_MUL            = 0x1A,  /**< r_dst = r_a * r_b. */
    AEGIS_OP_DIV            = 0x1B,  /**< r_dst = r_a / r_b. */
    AEGIS_OP_MOD            = 0x1C,  /**< r_dst = r_a % r_b. */
    AEGIS_OP_NEG            = 0x1D,  /**< r_dst = -r_a. */

    /* -- Bitwise & logic -- */
    AEGIS_OP_AND            = 0x1E,  /**< r_dst = r_a & r_b. */
    AEGIS_OP_OR             = 0x1F,  /**< r_dst = r_a | r_b. */
    AEGIS_OP_XOR            = 0x20,  /**< r_dst = r_a ^ r_b. */
    AEGIS_OP_NOT            = 0x21,  /**< r_dst = ~r_a. */

    /* -- Comparison -- */
    AEGIS_OP_EQ             = 0x22,  /**< r_dst = (r_a == r_b). */
    AEGIS_OP_NE             = 0x23,  /**< r_dst = (r_a != r_b). */
    AEGIS_OP_LT             = 0x24,  /**< r_dst = (r_a < r_b). */
    AEGIS_OP_LE             = 0x25,  /**< r_dst = (r_a <= r_b). */
    AEGIS_OP_GT             = 0x26,  /**< r_dst = (r_a > r_b). */
    AEGIS_OP_GE             = 0x27,  /**< r_dst = (r_a >= r_b). */

    /* -- Control flow -- */
    AEGIS_OP_JMP            = 0x28,  /**< Unconditional jump. */
    AEGIS_OP_JMP_IF         = 0x29,  /**< Jump if truthy. */
    AEGIS_OP_JMP_IF_NOT     = 0x2A,  /**< Jump if falsy. */

    /* -- Memory -- */
    AEGIS_OP_ALLOC          = 0x2B,  /**< Bump-allocate from heap arena. */
    AEGIS_OP_LOAD           = 0x2C,  /**< Load 16 bytes from arena. */
    AEGIS_OP_STORE          = 0x2D,  /**< Store 16 bytes to arena. */
    AEGIS_OP_STATIC_LOAD    = 0x2E,  /**< Load from static array. */
    AEGIS_OP_STATIC_STORE   = 0x2F,  /**< Store to static array. */
    AEGIS_OP_PUSH           = 0x30,  /**< Push register onto call stack. */
    AEGIS_OP_POP            = 0x31,  /**< Pop top of call stack. */

    /* -- Type conversion -- */
    AEGIS_OP_I32_TO_F32     = 0x32,  /**< Convert i32 to f32. */
    AEGIS_OP_F32_TO_I32     = 0x33,  /**< Convert f32 to i32. */
    AEGIS_OP_I64_TO_F64     = 0x34,  /**< Convert i64 to f64. */
    AEGIS_OP_F64_TO_I64     = 0x35,  /**< Convert f64 to i64. */
    AEGIS_OP_F64_TO_F32     = 0x36,  /**< Convert f64 to f32. */
    AEGIS_OP_F32_TO_F64     = 0x37,  /**< Convert f32 to f64. */

    /* -- Vector & quaternion math -- */
    AEGIS_OP_VEC3_ADD       = 0x38,  /**< vec3 addition. */
    AEGIS_OP_VEC3_SUB       = 0x39,  /**< vec3 subtraction. */
    AEGIS_OP_VEC3_MUL       = 0x3A,  /**< vec3 component-wise multiply. */
    AEGIS_OP_VEC3_SCALE     = 0x3B,  /**< vec3 scalar multiply. */
    AEGIS_OP_VEC3_DOT       = 0x3C,  /**< vec3 dot product. */
    AEGIS_OP_VEC3_CROSS     = 0x3D,  /**< vec3 cross product. */
    AEGIS_OP_VEC3_LEN       = 0x3E,  /**< vec3 length. */
    AEGIS_OP_VEC3_NORM      = 0x3F,  /**< vec3 normalize. */
    AEGIS_OP_QUAT_MUL       = 0x40,  /**< Quaternion multiply. */
    AEGIS_OP_QUAT_ROTATE    = 0x41,  /**< Rotate vec3 by quaternion. */

    /* -- Event signaling -- */
    AEGIS_OP_SIGNAL         = 0x42,  /**< Signal event to server (rate-limited). */
    AEGIS_OP_SUBSCRIBE      = 0x43,  /**< Subscribe to event topic. */
    AEGIS_OP_AWAIT_EVENT    = 0x44,  /**< Wait-yield for matching topic event. */

    /** Sentinel: total number of opcodes. Not a valid instruction. */
    AEGIS_OP_COUNT
} aegis_opcode_t;

/* ======================================================================= */
/* Instruction (§3.4)                                                       */
/* ======================================================================= */

/**
 * @brief 128-bit fixed-width instruction.
 *
 * Word 0: [opcode:16 | imm_flags:3 | reserved:13]
 * Words 1-3: operands A, B, C (register index or immediate value).
 *
 * Ownership: instructions are read-only once compiled into a bytecode module.
 */
typedef struct aegis_instruction {
    uint32_t words[4];
} aegis_instruction_t;

_Static_assert(sizeof(aegis_instruction_t) == 16, "instruction must be 16 bytes");

/* -- Immediate mode flag constants (bits 16-18 of word 0) -- */

/** Operand A is an immediate value (bit 16). */
#define AEGIS_IMM_A  (1u << 16)

/** Operand B is an immediate value (bit 17). */
#define AEGIS_IMM_B  (1u << 17)

/** Operand C is an immediate value (bit 18). */
#define AEGIS_IMM_C  (1u << 18)

/* -- Inline accessors -- */

/**
 * @brief Extract the opcode from an instruction (bits 0-15 of word 0).
 * @param insn Pointer to instruction. Must not be NULL.
 * @return Opcode value (aegis_opcode_t).
 */
static inline aegis_opcode_t aegis_insn_opcode(const aegis_instruction_t *insn) {
    return (aegis_opcode_t)(insn->words[0] & 0xFFFFu);
}

/**
 * @brief Check if operand A is an immediate value.
 * @param insn Pointer to instruction. Must not be NULL.
 */
static inline bool aegis_insn_imm_a(const aegis_instruction_t *insn) {
    return (insn->words[0] & AEGIS_IMM_A) != 0;
}

/**
 * @brief Check if operand B is an immediate value.
 * @param insn Pointer to instruction. Must not be NULL.
 */
static inline bool aegis_insn_imm_b(const aegis_instruction_t *insn) {
    return (insn->words[0] & AEGIS_IMM_B) != 0;
}

/**
 * @brief Check if operand C is an immediate value.
 * @param insn Pointer to instruction. Must not be NULL.
 */
static inline bool aegis_insn_imm_c(const aegis_instruction_t *insn) {
    return (insn->words[0] & AEGIS_IMM_C) != 0;
}

/**
 * @brief Extract operand A (word 1).
 * @param insn Pointer to instruction. Must not be NULL.
 */
static inline uint32_t aegis_insn_a(const aegis_instruction_t *insn) {
    return insn->words[1];
}

/**
 * @brief Extract operand B (word 2).
 * @param insn Pointer to instruction. Must not be NULL.
 */
static inline uint32_t aegis_insn_b(const aegis_instruction_t *insn) {
    return insn->words[2];
}

/**
 * @brief Extract operand C (word 3).
 * @param insn Pointer to instruction. Must not be NULL.
 */
static inline uint32_t aegis_insn_c(const aegis_instruction_t *insn) {
    return insn->words[3];
}

/**
 * @brief Build an instruction from opcode, flags, and operands.
 *
 * @param opcode   Instruction opcode (aegis_opcode_t).
 * @param flags    Immediate mode flags (AEGIS_IMM_A/B/C, OR'd together).
 * @param a        Operand A value (register index or immediate).
 * @param b        Operand B value (register index or immediate).
 * @param c        Operand C value (register index or immediate).
 * @return Encoded instruction.
 *
 * Side effects: none. Pure function.
 */
static inline aegis_instruction_t aegis_insn_make(
    aegis_opcode_t opcode, uint32_t flags,
    uint32_t a, uint32_t b, uint32_t c)
{
    aegis_instruction_t insn;
    insn.words[0] = ((uint32_t)opcode & 0xFFFFu) | (flags & 0x00070000u);
    insn.words[1] = a;
    insn.words[2] = b;
    insn.words[3] = c;
    return insn;
}

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_TYPES_H */
