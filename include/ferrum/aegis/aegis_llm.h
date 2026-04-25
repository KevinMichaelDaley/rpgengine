/**
 * @file aegis_llm.h
 * @brief LLM result and tool-call types for AEGIS VM integration.
 *
 * Variable-length structs written into the script heap arena by the
 * LLM executor. Scripts read these via LOAD/STORE after wait completes.
 */
#ifndef FERRUM_AEGIS_LLM_H
#define FERRUM_AEGIS_LLM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── Result status codes ───────────────────────────────────────── */

enum {
    AEGIS_LLM_OK              = 0,  /**< Success. */
    AEGIS_LLM_ERROR           = -1, /**< Generic error (HTTP, parse). */
    AEGIS_LLM_TIMEOUT         = -2, /**< Request exceeded timeout. */
    AEGIS_LLM_BUDGET_EXCEEDED = -3  /**< Global budget limit reached. */
};

/* ── Tool call ─────────────────────────────────────────────────── */

/**
 * @brief A single tool_call from an LLM response.
 *
 * Variable-length: id, name, and args are packed into data[] as
 * three consecutive null-terminated strings.
 */
typedef struct aegis_llm_tool_call {
    uint32_t id_len;   /**< Length of tool call ID (bytes). */
    uint32_t name_len; /**< Length of function name (bytes). */
    uint32_t args_len; /**< Length of JSON arguments (bytes). */
    char     data[];   /**< id\0name\0args\0 */
} aegis_llm_tool_call_t;

/* ── Result header ─────────────────────────────────────────────── */

/**
 * @brief LLM prompt result written to heap arena.
 *
 * Variable-length: response text and tool calls follow the fixed header.
 * Layout:
 *   [aegis_llm_result_t header]
 *   [response_len bytes of UTF-8 text + '\0']
 *   [tool_call_count × aegis_llm_tool_call_t]
 */
typedef struct aegis_llm_result {
    int32_t  status;          /**< AEGIS_LLM_OK, ERROR, TIMEOUT, BUDGET_EXCEEDED. */
    int32_t  input_tokens;    /**< Tokens consumed in prompt. */
    int32_t  output_tokens;   /**< Tokens generated in response. */
    float    cost_usd;        /**< Estimated cost of this prompt. */
    float    total_cost_usd;  /**< Cumulative cost since engine start. */
    uint32_t response_len;    /**< Bytes in response text (excl. null). */
    uint32_t tool_call_count; /**< Number of tool calls in response. */
    char     response[];      /**< Null-terminated UTF-8 response text. */
    /* Followed by tool_call_count × aegis_llm_tool_call_t structs. */
} aegis_llm_result_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_LLM_H */
