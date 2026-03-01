/**
 * @file aegis_ops_flow.c
 * @brief Control flow ops: jmp, jmp_if, jmp_if_not, call.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 */

#include "ferrum/aegis/aegis_ops_flow.h"

bool aegis_op_jmp(uint32_t label, uint32_t bytecode_len, uint32_t *out_pc) {
    if (label >= bytecode_len) {
        return false;
    }
    *out_pc = label;
    return true;
}

bool aegis_op_jmp_if(const aegis_register_t *cond, uint32_t label,
                     uint32_t current_pc, uint32_t bytecode_len,
                     uint32_t *out_pc) {
    if (cond->u32 != 0) {
        if (label >= bytecode_len) {
            return false;
        }
        *out_pc = label;
    } else {
        *out_pc = current_pc + 1;
    }
    return true;
}

bool aegis_op_jmp_if_not(const aegis_register_t *cond, uint32_t label,
                         uint32_t current_pc, uint32_t bytecode_len,
                         uint32_t *out_pc) {
    if (cond->u32 == 0) {
        if (label >= bytecode_len) {
            return false;
        }
        *out_pc = label;
    } else {
        *out_pc = current_pc + 1;
    }
    return true;
}

bool aegis_op_call(aegis_memory_t *mem, uint32_t current_pc,
                   uint32_t label, uint32_t bytecode_len,
                   uint32_t *out_pc) {
    if (label >= bytecode_len) {
        return false;
    }
    /* Push return address (instruction after call). */
    if (!aegis_memory_push_frame(mem, current_pc + 1)) {
        return false;
    }
    *out_pc = label;
    return true;
}
