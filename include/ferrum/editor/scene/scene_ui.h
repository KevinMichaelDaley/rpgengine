/**
 * @file scene_ui.h
 * @brief Scene editor UI panel builders (Clay layout).
 *
 * Each function builds the Clay layout for one panel region.
 * Called during the layout phase of each frame, between
 * Clay_BeginLayout() and Clay_EndLayout().
 *
 * Public types: scene_ui_action_t, scene_ui_state_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_UI_H
#define FERRUM_EDITOR_SCENE_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct scene_editor;
struct panel_rect;

/* ---- Types ---- */

/**
 * @brief UI action produced by interactive elements.
 *
 * Set by Clay_OnHover callbacks during layout. The scene editor
 * reads these after Clay_EndLayout() and dispatches commands.
 */
typedef enum scene_ui_action {
    UI_ACTION_NONE = 0,
    UI_ACTION_SPAWN_BOX,
    UI_ACTION_SPAWN_SPHERE,
    UI_ACTION_SPAWN_CAPSULE,
    UI_ACTION_SELECT_ENTITY,    /**< entity_id in action_target. */
    UI_ACTION_DESELECT_ENTITY,  /**< entity_id in action_target. */
    UI_ACTION_DELETE_SELECTED,
    UI_ACTION_MODE_NONE,        /**< Disable gizmo (selection-only). */
    UI_ACTION_MODE_TRANSLATE,
    UI_ACTION_MODE_ROTATE,
    UI_ACTION_MODE_SCALE,
    UI_ACTION_TUI_COMMAND,      /**< Execute TUI command (text in tui_cmd). */
} scene_ui_action_t;

/* Transform mode values for scene_ui_state_t.transform_mode. */
#define UI_MODE_NONE      -1
#define UI_MODE_TRANSLATE 0
#define UI_MODE_ROTATE    1
#define UI_MODE_SCALE     2

/** Maximum command input line length. */
#define UI_TUI_INPUT_MAX 256

/** Maximum log lines in the TUI scrollback. */
#define UI_TUI_LOG_MAX   64

/** Maximum command history entries. */
#define UI_TUI_HISTORY_MAX 32

/** Maximum offline command queue depth. */
#define UI_TUI_OFFLINE_Q_MAX 8192


/** Maximum buffered UX actions during pending deletes. */
#define UI_ACTION_Q_MAX 64

/** Frames between automatic delete retry attempts. */
#define UI_DELETE_RETRY_INTERVAL 10

/** Maximum characters per log line. */
#define UI_TUI_LOG_LINE  128

/** TUI log line types / status. */
#define UI_TUI_LOG_NORMAL  0  /**< Plain text, no status column. */
#define UI_TUI_LOG_ERROR   1  /**< Red X status. */
#define UI_TUI_LOG_SUCCESS 2  /**< Green checkmark status. */
#define UI_TUI_LOG_PENDING 3  /**< Awaiting server response (shows spinner/dot). */

/**
 * @brief Mutable UI state shared across panel builders.
 *
 * Tracks scroll offsets, pending actions, and hover state.
 */
typedef struct scene_ui_state {
    /* Pending action from this frame's layout. */
    scene_ui_action_t action;
    uint32_t          action_target;   /**< Entity ID for select/deselect. */

    /* Outliner state. */
    int               outliner_scroll; /**< Scroll offset in pixels. */

    /* Inspector state. */
    int               inspector_scroll;

    /* Mouse state for Clay interaction. */
    float             mouse_x;
    float             mouse_y;
    bool              mouse_down;
    bool              mouse_was_down;  /**< Previous frame mouse state. */
    bool              mouse_clicked;   /**< True if press occurred this frame. */
    bool              middle_mouse_down; /**< Middle mouse button state. */
    bool              right_mouse_down;  /**< Right mouse button state. */
    float             scroll_delta_y;  /**< Accumulated scroll wheel delta this frame. */

    /* Entity counter for auto-naming. */
    uint32_t          spawn_counter;

    /* Transform mode: UI_MODE_TRANSLATE, UI_MODE_ROTATE, UI_MODE_SCALE. */
    uint8_t           transform_mode;

    /* TUI command input. */
    char              tui_input[UI_TUI_INPUT_MAX]; /**< Current input text. */
    int               tui_cursor;    /**< Cursor position in input. */
    int               tui_input_len; /**< Length of input text. */
    bool              tui_active;    /**< True when TUI has keyboard focus. */
    bool              tui_skip_next_text; /**< Skip next SDL_TEXTINPUT (colon). */
    char              tui_cmd[UI_TUI_INPUT_MAX]; /**< Last submitted command. */

    /* TUI log ring buffer (circular, newest at tui_log_head-1). */
    char              tui_log[UI_TUI_LOG_MAX][UI_TUI_LOG_LINE];
    uint8_t           tui_log_type[UI_TUI_LOG_MAX]; /**< UI_TUI_LOG_* status. */
    uint32_t          tui_log_cmd_id[UI_TUI_LOG_MAX]; /**< Cmd ID for pending entries. */
    uint32_t          tui_log_timestamp[UI_TUI_LOG_MAX]; /**< SDL_GetTicks at creation. */
    int               tui_log_head;  /**< Next write slot (wraps). */
    int               tui_log_count; /**< Number of lines stored. */
    int               tui_log_scroll; /**< Lines scrolled back from newest (0=bottom). */
    int               tui_log_visible; /**< Visible lines (set each frame). */

    /* Command history ring buffer. */
    char              tui_history[UI_TUI_HISTORY_MAX][UI_TUI_INPUT_MAX];
    int               tui_history_head;   /**< Next write slot (wraps). */
    int               tui_history_count;  /**< Number of stored commands. */
    int               tui_history_index;  /**< Browse position (-1=not browsing). */
    char              tui_history_stash[UI_TUI_INPUT_MAX]; /**< Stashed input. */

    /* Outliner scroll state. */
    int               outliner_total;   /**< Total entity rows (set each frame). */
    int               outliner_visible_lines; /**< Visible rows (set each frame). */

    /* Inspector scroll state. */
    int               inspector_total;  /**< Total content height px (set each frame). */
    int               inspector_visible_lines; /**< Visible height px (set each frame). */

    /* Scrollbar drag state. */
    int               scrollbar_dragging;  /**< 0=none, 1=outliner, 2=inspector, 3=tui. */
    float             scrollbar_drag_y;    /**< Mouse Y at drag start (logical px). */
    int               scrollbar_drag_scroll; /**< Scroll value at drag start. */

    /* Pending delete tracking: entity IDs awaiting confirmed server deletion.
     * Survives entity list refresh (entity struct gets overwritten).
     * Entities in this list are greyed/hidden in the outliner.
     * vm_reserve'd array of entity IDs (demand-paged, up to entity cap). */
    uint32_t         *pending_delete_ids;      /**< Entity IDs pending delete. */
    uint32_t         *pending_delete_log_ids; /**< Cmd ID of original delete (for log resolve). */
    uint32_t          pending_delete_count;   /**< Number of pending IDs. */
    uint32_t          pending_delete_cap;     /**< Array capacity. */

    /* Offline command queue: commands entered while disconnected.
     * Heap-allocated ring buffer, flushed on reconnect. */
    char            (*offline_q)[UI_TUI_INPUT_MAX]; /**< Ring buffer (heap). */
    int               offline_q_head;   /**< Next write slot. */
    int               offline_q_count;  /**< Number queued. */

    /* In-flight delete command ID tracking.
     * vm_reserve'd array sized to entity cap (worst case: select-all + delete).
     * Used to suppress error logging for delete commands and
     * silently retry on failure. */
    uint32_t         *delete_cmd_ids;     /**< Tracked delete cmd IDs. */
    uint32_t          delete_cmd_id_count;
    uint32_t          delete_cmd_id_cap;  /**< Array capacity. */
    uint32_t          delete_retry_timer; /**< Frames until next auto-retry. */

    /* Buffered UX actions: queued while deletes are pending.
     * Replayed when pending_delete_count drops to 0. */
    scene_ui_action_t action_q_actions[UI_ACTION_Q_MAX];
    uint32_t          action_q_targets[UI_ACTION_Q_MAX];
    int               action_q_count;
} scene_ui_state_t;

/**
 * @brief Append a line to the TUI log ring buffer.
 * @param ui   UI state (non-NULL).
 * @param text Log line text (non-NULL, truncated to UI_TUI_LOG_LINE-1).
 */
void scene_ui_tui_log(scene_ui_state_t *ui, const char *text);

/**
 * @brief Append an error line to the TUI log ring buffer (rendered in red).
 * @param ui   UI state (non-NULL).
 * @param text Log line text (non-NULL, truncated to UI_TUI_LOG_LINE-1).
 */
void scene_ui_tui_log_error(scene_ui_state_t *ui, const char *text);

/**
 * @brief Append a success line to the TUI log (rendered with green checkmark).
 * @param ui   UI state (non-NULL).
 * @param text Log line text (non-NULL, truncated to UI_TUI_LOG_LINE-1).
 */
void scene_ui_tui_log_success(scene_ui_state_t *ui, const char *text);

/**
 * @brief Append a pending command line to the TUI log.
 *
 * Displayed with a pending indicator until resolved by cmd_id.
 *
 * @param ui     UI state (non-NULL).
 * @param text   Log line text (non-NULL).
 * @param cmd_id Command ID for later status backfill.
 */
void scene_ui_tui_log_pending(scene_ui_state_t *ui, const char *text,
                               uint32_t cmd_id);

/**
 * @brief Backfill a pending log entry's status by command ID.
 *
 * Searches the log ring buffer for a PENDING entry with the given cmd_id
 * and updates its type to SUCCESS or ERROR.
 *
 * @param ui     UI state (non-NULL).
 * @param cmd_id Command ID to match.
 * @param ok     True for success, false for error.
 */
void scene_ui_tui_log_resolve(scene_ui_state_t *ui, uint32_t cmd_id, bool ok);

/* ---- Panel builders ---- */

/**
 * @brief Build the outliner panel: create buttons + entity list.
 */
void scene_ui_build_outliner(struct scene_editor *ed,
                             const struct panel_rect *rect);

/**
 * @brief Build the inspector panel: selected entity properties.
 */
void scene_ui_build_inspector(struct scene_editor *ed,
                              const struct panel_rect *rect);

/**
 * @brief Build the viewport panel: 2D top-down entity view.
 */
void scene_ui_build_viewport(struct scene_editor *ed,
                             const struct panel_rect *rect);

/**
 * @brief Build the TUI panel: status bar and log.
 */
void scene_ui_build_tui(struct scene_editor *ed,
                        const struct panel_rect *rect);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_UI_H */
