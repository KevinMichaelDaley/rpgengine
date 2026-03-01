/**
 * @file aegis_ops_flow2.c
 * @brief Control flow op: ret.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 */

#include "ferrum/aegis/aegis_ops_flow.h"

bool aegis_op_ret(aegis_memory_t *mem, uint32_t *out_pc) {
    return aegis_memory_pop_frame(mem, out_pc);
}
