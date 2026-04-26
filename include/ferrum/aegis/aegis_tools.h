/**
 * @file aegis_tools.h
 * @brief Tool IDs, error codes, and result types for AEGIS VM tool_action.
 *
 * Defines the whitelist of tools the LLM may invoke via the tool_action
 * opcode (AEGIS_OP_TOOL_ACTION = 0x4D). All tools are immediate (no async)
 * except SENSE_QUERY and KNOWLEDGE_QUERY which have their own opcodes.
 */

#ifndef FERRUM_AEGIS_TOOLS_H
#define FERRUM_AEGIS_TOOLS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ======================================================================= */
/* Tool IDs (whitelist)                                                    */
/* ======================================================================= */

typedef enum {
    AEGIS_TOOL_TRADE_INIT   = 0,  /**< Attempt to initialize trade loop. */
    AEGIS_TOOL_TRADE_SELL   = 1,  /**< Set offer item or broadcast sell. */
    AEGIS_TOOL_TRADE_BUY    = 2,  /**< Set ask item or broadcast want-to-buy. */
    AEGIS_TOOL_TRADE_ACCEPT = 3,  /**< Accept current trade offer. */
    AEGIS_TOOL_TRADE_REJECT = 4,  /**< Reject and exit trade loop. */
    AEGIS_TOOL_DEFEND       = 5,  /**< Set combat BT to protect target. */
    AEGIS_TOOL_ATTACK       = 6,  /**< Set combat BT to engage target. */
    AEGIS_TOOL_FLEE         = 7,  /**< Set combat BT to escape. */
    AEGIS_TOOL_GOTO         = 8,  /**< Submit nav query for target. */
    AEGIS_TOOL_KNOWLEDGE_QUERY = 9, /**< Semantic search over knowledge graph. */
    AEGIS_TOOL_COUNT        = 10  /**< Sentinel / max valid ID + 1. */
} aegis_tool_id_t;

/* ======================================================================= */
/* Error codes returned in result register                                 */
/* ======================================================================= */

#define AEGIS_TOOL_OK          0
#define AEGIS_TOOL_UNKNOWN    -1
#define AEGIS_TOOL_RANGE      -2
#define AEGIS_TOOL_LANGUAGE   -3
#define AEGIS_TOOL_STATE      -4
#define AEGIS_TOOL_INVENTORY  -5
#define AEGIS_TOOL_NAV        -6

/* ======================================================================= */
/* Tool result layout (written to heap arena)                              */
/* ======================================================================= */

/**
 * @brief Result of a tool_action invocation.
 *
 * Written to a heap-allocated slot by the opcode handler.
 * status: AEGIS_TOOL_OK or negative error code.
 * message: null-terminated human-readable text returned to the LLM.
 */
typedef struct aegis_tool_result {
    int32_t status;   /**< AEGIS_TOOL_OK or negative error code. */
    char    message[]; /**< Null-terminated status / error text. */
} aegis_tool_result_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_TOOLS_H */
