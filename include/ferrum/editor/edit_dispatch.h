/**
 * @file edit_dispatch.h
 * @brief Editor command dispatch framework.
 *
 * Provides a command handler table and drain loop. The drain function
 * is called once per tick (Stage 1, after physics). It pops commands
 * from the SPSC ring, dispatches to the appropriate handler, records
 * undo entries, and pushes JSON responses into the response ring.
 *
 * Thread safety: all functions must be called from the main tick thread.
 */
#ifndef FERRUM_EDITOR_EDIT_DISPATCH_H
#define FERRUM_EDITOR_EDIT_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/editor/json_parse.h"

/* Forward declarations. */
typedef struct edit_dispatch edit_dispatch_t;

/* ------------------------------------------------------------------------ */
/* Configuration                                                             */
/* ------------------------------------------------------------------------ */

/** @brief Maximum registered command handlers. */
#define EDIT_DISPATCH_MAX_HANDLERS  64

/** @brief Maximum command name length. */
#define EDIT_DISPATCH_MAX_CMD_NAME  32

/* ------------------------------------------------------------------------ */
/* Handler signature                                                         */
/* ------------------------------------------------------------------------ */

/**
 * @brief Command handler function.
 *
 * @param dispatch  Dispatch context (provides access to editor state).
 * @param args      Parsed JSON "args" object from the command (may be NULL).
 * @param result    Arena for building the response JSON value.
 * @param arena     Arena for allocating response values.
 * @return true on success, false on error.
 */
typedef bool (*edit_cmd_handler_fn)(edit_dispatch_t *dispatch,
                                    const json_value_t *args,
                                    json_value_t *result,
                                    json_arena_t *arena);

/**
 * @brief A registered command handler entry.
 */
typedef struct edit_cmd_handler_entry {
    char                 name[EDIT_DISPATCH_MAX_CMD_NAME]; /**< Command name. */
    edit_cmd_handler_fn  handler;                          /**< Handler fn. */
} edit_cmd_handler_entry_t;

/**
 * @brief Dispatch table and context.
 *
 * Stores the handler table, parse/response arenas, and references
 * to the command/response rings.
 *
 * Ownership:
 * - The dispatch owns its handler table and arenas.
 * - Ring pointers are borrowed.
 */
typedef struct edit_dispatch {
    edit_cmd_handler_entry_t handlers[EDIT_DISPATCH_MAX_HANDLERS];
    uint32_t                 handler_count;

    /* Arenas for parsing incoming commands and building responses. */
    uint8_t *parse_arena_buf;    /**< Backing buffer for parse arena. */
    size_t   parse_arena_cap;    /**< Size of parse arena. */
    uint8_t *resp_arena_buf;     /**< Backing buffer for response arena. */
    size_t   resp_arena_cap;     /**< Size of response arena. */

    void *user_data;             /**< Opaque pointer to editor_ctx. */
} edit_dispatch_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the dispatch table.
 *
 * @param dispatch   Dispatch context.
 * @param arena_size Size for parse and response arenas (each).
 * @param user_data  Opaque pointer passed to handlers (e.g., editor_ctx).
 * @return true on success.
 */
bool edit_dispatch_init(edit_dispatch_t *dispatch, size_t arena_size,
                        void *user_data);

/**
 * @brief Free dispatch resources.
 * @param dispatch  Dispatch context.
 */
void edit_dispatch_destroy(edit_dispatch_t *dispatch);

/* ------------------------------------------------------------------------ */
/* Handler registration                                                      */
/* ------------------------------------------------------------------------ */

/**
 * @brief Register a command handler.
 *
 * @param dispatch  Dispatch context.
 * @param name      Command name (e.g., "spawn", "move", "undo").
 * @param handler   Handler function.
 * @return true on success, false if table full or name too long.
 */
bool edit_dispatch_register(edit_dispatch_t *dispatch, const char *name,
                            edit_cmd_handler_fn handler);

/**
 * @brief Look up a handler by command name.
 *
 * @param dispatch  Dispatch context.
 * @param name      Command name to look up.
 * @param name_len  Length of name string.
 * @return Handler function, or NULL if not found.
 */
edit_cmd_handler_fn edit_dispatch_lookup(const edit_dispatch_t *dispatch,
                                         const char *name, uint32_t name_len);

/* ------------------------------------------------------------------------ */
/* Dispatch execution                                                        */
/* ------------------------------------------------------------------------ */

/**
 * @brief Execute a single JSON command string and write a JSON response.
 *
 * Parses the command JSON, looks up the handler, calls it, and
 * writes the response JSON into resp_buf.
 *
 * @param dispatch  Dispatch context.
 * @param json      Raw JSON command string.
 * @param json_len  Length of JSON string.
 * @param resp_buf  Buffer for response JSON.
 * @param resp_cap  Capacity of response buffer.
 * @return Number of bytes written to resp_buf, or 0 on error.
 */
uint32_t edit_dispatch_exec(edit_dispatch_t *dispatch,
                            const char *json, uint32_t json_len,
                            char *resp_buf, uint32_t resp_cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_DISPATCH_H */
