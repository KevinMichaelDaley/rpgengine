/**
 * @file aegis_decode.c
 * @brief Aegis instruction decoder implementation.
 *
 * Per ref/aegis_bytecode_spec.md §3.4.
 */

#include "ferrum/aegis/aegis_decode.h"
#include <string.h>

bool aegis_decode(const aegis_instruction_t *insn,
                  const aegis_register_t regs[AEGIS_REGISTER_COUNT],
                  aegis_decode_result_t *result) {
    result->opcode = aegis_insn_opcode(insn);
    result->raw_a  = aegis_insn_a(insn);
    result->raw_b  = aegis_insn_b(insn);
    result->raw_c  = aegis_insn_c(insn);
    result->imm_a  = aegis_insn_imm_a(insn);
    result->imm_b  = aegis_insn_imm_b(insn);
    result->imm_c  = aegis_insn_imm_c(insn);

    /* Resolve operand A. */
    if (result->imm_a) {
        memset(&result->a, 0, sizeof(result->a));
        result->a.u32 = result->raw_a;
    } else {
        if (result->raw_a > 255) {
            return false;
        }
        result->a = regs[result->raw_a];
    }

    /* Resolve operand B. */
    if (result->imm_b) {
        memset(&result->b, 0, sizeof(result->b));
        result->b.u32 = result->raw_b;
    } else {
        if (result->raw_b > 255) {
            return false;
        }
        result->b = regs[result->raw_b];
    }

    /* Resolve operand C. */
    if (result->imm_c) {
        memset(&result->c, 0, sizeof(result->c));
        result->c.u32 = result->raw_c;
    } else {
        if (result->raw_c > 255) {
            return false;
        }
        result->c = regs[result->raw_c];
    }

    return true;
}
