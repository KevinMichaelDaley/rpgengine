/**
 * @file inline_field.h
 * @brief Inline editable field widget for inspector panels.
 *
 * Provides a click-to-edit numeric field for Clay UI. When clicked,
 * the field enters edit mode — keyboard input modifies the text buffer,
 * Enter commits, Escape cancels.
 *
 * Thread safety: must be accessed from the main render thread only.
 */
#ifndef FERRUM_EDITOR_UI_INLINE_FIELD_H
#define FERRUM_EDITOR_UI_INLINE_FIELD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief Maximum characters in the edit buffer. */
#define INLINE_FIELD_BUF_MAX 32

/** @brief Virtual key codes for handle_key (SDL-independent). */
typedef enum inline_field_key {
    INLINE_FIELD_KEY_BACKSPACE = 1,
    INLINE_FIELD_KEY_DELETE    = 2,
    INLINE_FIELD_KEY_LEFT      = 3,
    INLINE_FIELD_KEY_RIGHT     = 4,
    INLINE_FIELD_KEY_HOME      = 5,
    INLINE_FIELD_KEY_END       = 6,
    INLINE_FIELD_KEY_ENTER     = 7,
    INLINE_FIELD_KEY_ESCAPE    = 8,
    INLINE_FIELD_KEY_UP        = 9,
    INLINE_FIELD_KEY_DOWN      = 10,
} inline_field_key_t;

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief State for a single inline editable field.
 *
 * One instance per editable property in the inspector.
 */
typedef struct inline_field_state {
    bool     active;                         /**< Currently being edited. */
    char     buf[INLINE_FIELD_BUF_MAX];      /**< Edit text buffer. */
    uint32_t cursor;                         /**< Cursor position in buf. */
    float    original_value;                 /**< Value before edit started. */
    uint32_t field_id;                       /**< Unique field identifier. */
    float   *target;                         /**< Write-back pointer (commit writes here). */
} inline_field_state_t;

/**
 * @brief Context tracking the currently active inline field.
 *
 * Only one field can be active at a time. Stored in scene_editor_t.
 */
typedef struct inline_field_ctx {
    inline_field_state_t *active_field;      /**< Currently editing (NULL = none). */
} inline_field_ctx_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Begin editing a field.
 *
 * Populates the edit buffer with the formatted current value and
 * sets the field as active. If another field was active, it is cancelled.
 *
 * @param ctx        Context (tracks active field).
 * @param field      Field state to activate.
 * @param field_id   Unique identifier for this field.
 * @param value      Current float value to display in the edit buffer.
 */
void inline_field_begin(inline_field_ctx_t *ctx,
                         inline_field_state_t *field,
                         uint32_t field_id, float value);

/**
 * @brief Cancel editing and restore the original value.
 *
 * @param ctx        Context.
 * @param out_value  Receives the original value (may be NULL).
 */
void inline_field_cancel(inline_field_ctx_t *ctx, float *out_value);

/**
 * @brief Commit the edit buffer and parse the new value.
 *
 * @param ctx        Context.
 * @param out_value  Receives the parsed float value.
 * @return true if parse succeeded, false on empty/invalid input.
 */
bool inline_field_commit(inline_field_ctx_t *ctx, float *out_value);

/* ------------------------------------------------------------------------ */
/* Input handling                                                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Handle a virtual key press while a field is active.
 *
 * @param ctx  Context (must have an active field).
 * @param key  Virtual key code.
 * @return true if the key was consumed.
 */
bool inline_field_handle_key(inline_field_ctx_t *ctx, inline_field_key_t key);

/**
 * @brief Handle a text character input (from SDL_TEXTINPUT).
 *
 * Only accepts digits, decimal point, and minus sign.
 *
 * @param ctx  Context.
 * @param ch   Character to insert.
 * @return true if the character was accepted.
 */
bool inline_field_handle_text(inline_field_ctx_t *ctx, char ch);

/**
 * @brief Check if any field is currently being edited.
 */
static inline bool inline_field_is_active(const inline_field_ctx_t *ctx) {
    return ctx && ctx->active_field && ctx->active_field->active;
}

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_UI_INLINE_FIELD_H */
