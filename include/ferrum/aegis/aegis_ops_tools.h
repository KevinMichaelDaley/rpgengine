/**
 * @file aegis_ops_tools.h
 * @brief Tool action opcode handler for AEGIS VM.
 *
 * Declares aegis_op_tool_action(), which validates the tool_id whitelist,
 * parses JSON args from the heap arena, dispatches to the appropriate
 * tool handler (stub or real), and writes a result string back to the
 * heap arena.
 */

#ifndef FERRUM_AEGIS_OPS_TOOLS_H
#define FERRUM_AEGIS_OPS_TOOLS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

struct aegis_vm;
struct aegis_decode_result;
struct npc_knowledge_graph;

/**
 * @brief Execute a tool_action opcode (tool_action r_result, r_tool_id, r_args_handle).
 *
 * Validates tool_id against the whitelist (0-9). Parses JSON args from the
 * heap offset in r_args_handle. Dispatches to the tool-specific handler.
 * Writes a heap-allocated result string and stores its offset in r_result.
 *
 * Fuel cost: 50 (deducted by caller / interpreter).
 *
 * @return true on successful dispatch, false if tool_id is unknown.
 */
bool aegis_op_tool_action(struct aegis_vm *vm, const struct aegis_decode_result *d);

bool aegis_op_knowledge_query(struct aegis_vm *vm, const char *args_json);

bool aegis_op_related_entities(struct aegis_vm *vm, const char *args_json);

bool aegis_op_kg_path(struct aegis_vm *vm, const char *args_json);

void aegis_set_knowledge_graph(struct npc_knowledge_graph *kg);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_OPS_TOOLS_H */
