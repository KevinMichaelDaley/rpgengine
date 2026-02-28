/**
 * @file client_editor_input.h
 * @brief Editor input processor: mouse/viewport events → push events.
 *
 * Converts raw mouse interactions (clicks, drags, right-clicks) into
 * structured push events sent to the controller via the state socket.
 *
 * Thread safety: single-threaded (client main thread only).
 */
#ifndef FERRUM_EDITOR_CLIENT_EDITOR_INPUT_H
#define FERRUM_EDITOR_CLIENT_EDITOR_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct client_state_dispatch;

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Editor input state.
 *
 * Tracks mouse state for drag detection and box selection.
 */
typedef struct editor_input {
    struct client_state_dispatch *dispatch; /**< For sending push events. */

    float    drag_start[3];   /**< World pos where drag started. */
    bool     dragging;        /**< True if currently in a drag. */
    uint32_t hover_entity;    /**< Entity under cursor (0 = none). */
} editor_input_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize editor input state.
 * @param input     Input state (non-NULL).
 * @param dispatch  Dispatch context for push events (non-NULL).
 */
void editor_input_init(editor_input_t *input,
                       struct client_state_dispatch *dispatch);

/* ------------------------------------------------------------------------ */
/* Event processing                                                          */
/* ------------------------------------------------------------------------ */

/**
 * @brief Handle a left-click at a world position.
 *
 * If an entity is under the cursor, pushes entity_clicked.
 * Otherwise, pushes cursor_moved.
 *
 * @param input      Input state.
 * @param entity_id  Entity under cursor (0 = none).
 * @param wx, wy, wz  World position of the click.
 */
void editor_input_click(editor_input_t *input, uint32_t entity_id,
                        float wx, float wy, float wz);

/**
 * @brief Handle a right-click at a world position.
 *
 * Pushes a context_menu event.
 *
 * @param input      Input state.
 * @param entity_id  Entity under cursor (0 = none).
 * @param wx, wy, wz  World position.
 */
void editor_input_right_click(editor_input_t *input, uint32_t entity_id,
                              float wx, float wy, float wz);

/**
 * @brief Handle a box-select completion.
 *
 * Pushes a box_select event with the list of entity IDs in the box.
 *
 * @param input       Input state.
 * @param entity_ids  Array of entity IDs in the selection box.
 * @param count       Number of entities.
 */
void editor_input_box_select(editor_input_t *input,
                             const uint32_t *entity_ids, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CLIENT_EDITOR_INPUT_H */
