/**
 * @file edit_cmd_ctx.h
 * @brief Editor command context — shared state for command handlers.
 *
 * Command handlers access entity storage, selection, and undo stack
 * through this context. Stored as dispatch->user_data.
 *
 * Thread safety: only accessed from the main tick thread during drain.
 */
#ifndef FERRUM_EDITOR_EDIT_CMD_CTX_H
#define FERRUM_EDITOR_EDIT_CMD_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Forward declarations (full types in their own headers). */
struct edit_entity_store;
struct edit_selection;
struct edit_undo_stack;

/* ------------------------------------------------------------------------ */
/* Command type tags (for undo entries)                                      */
/* ------------------------------------------------------------------------ */

/**
 * @brief Command type tags used in undo entry forward/inverse_type fields.
 */
typedef enum edit_cmd_type {
    EDIT_CMD_TYPE_SPAWN   = 1,
    EDIT_CMD_TYPE_DELETE  = 2,
    EDIT_CMD_TYPE_MOVE    = 3,
    EDIT_CMD_TYPE_ROTATE  = 4,
    EDIT_CMD_TYPE_SCALE   = 5,
} edit_cmd_type_t;

/* ------------------------------------------------------------------------ */
/* Command context                                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief Shared context for all entity command handlers.
 *
 * Stored as dispatch->user_data. Handlers cast user_data to this type.
 */
typedef struct edit_cmd_ctx {
    struct edit_entity_store *entities;   /**< Entity storage. */
    struct edit_selection    *selection;  /**< Current selection set. */
    struct edit_undo_stack   *undo;      /**< Undo/redo stack. */
} edit_cmd_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_CMD_CTX_H */
