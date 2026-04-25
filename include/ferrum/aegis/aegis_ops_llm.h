/**
 * @file aegis_ops_llm.h
 * @brief LLM prompt opcode handler for AEGIS VM.
 *
 * Declares aegis_op_llm_prompt(), which submits an async LLM prompt
 * task to the MPSC buffer. Follows the same pattern as vis_test and
 * nav_query.
 */
#ifndef FERRUM_AEGIS_OPS_LLM_H
#define FERRUM_AEGIS_OPS_LLM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

struct aegis_vm;
struct aegis_decode_result;

/**
 * @brief Submit an async LLM prompt (llm_prompt r_handle, r_prompt_off, r_max_tokens).
 *
 * Reads a null-terminated UTF-8 prompt string from the heap arena at
 * offset regs[r_prompt_off].i32, clamps max_tokens against engine
 * settings, builds an AEGIS_TASK_LLM_PROMPT task, and submits it to
 * the async buffer.
 *
 * Fuel cost: 500 (deducted by the interpreter).
 *
 * @return true on success, false on limit/buffer/heap error.
 */
bool aegis_op_llm_prompt(struct aegis_vm *vm, const struct aegis_decode_result *d);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_OPS_LLM_H */
