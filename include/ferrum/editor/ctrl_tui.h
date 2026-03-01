/**
 * @file ctrl_tui.h
 * @brief Controller TUI context — terminal rendering and input handling.
 *
 * The controller is a standalone curses-style process that connects to
 * the editor server over TCP. Uses raw termios, ANSI escape rendering,
 * and poll()-based event loop.
 *
 * Thread safety: single-threaded (controller main loop only).
 */
#ifndef FERRUM_EDITOR_CTRL_TUI_H
#define FERRUM_EDITOR_CTRL_TUI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ferrum/editor/ctrl_log.h"

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

#define CTRL_CMD_MAX_LEN   1024
#define CTRL_SCREEN_BUF    (64 * 1024)

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief TUI input mode.
 *
 * Determines how keystrokes are interpreted.
 */
typedef enum ctrl_input_mode {
    CTRL_MODE_NORMAL  = 0,  /**< Default: hotkeys, vim-style prefix. */
    CTRL_MODE_COMMAND = 1,  /**< Command-line editing (after ':'). */
    CTRL_MODE_REPL    = 2,  /**< Script REPL mode. */
    CTRL_MODE_GRAB    = 3,  /**< Entity grab mode. */
    CTRL_MODE_CONTEXT = 4,  /**< Context menu mode. */
} ctrl_input_mode_t;

/**
 * @brief TUI context — owns all controller display and input state.
 *
 * The command line, log area, input mode, and screen buffer are
 * all embedded here. Connection file descriptors are set externally
 * after init.
 */
typedef struct ctrl_tui {
    /* Input mode state machine. */
    ctrl_input_mode_t mode;         /**< Current input mode. */
    uint32_t          numeric_prefix; /**< Accumulated numeric prefix (Normal). */

    /* Command line (bottom bar). */
    char     cmd_text[CTRL_CMD_MAX_LEN]; /**< Current command text. */
    uint32_t cmd_len;                    /**< Length of command text. */
    uint32_t cmd_cursor;                 /**< Cursor position within text. */

    /* Log area. */
    ctrl_log_t log;                 /**< Log ring buffer. */

    /* Terminal dimensions. */
    int cols;                       /**< Terminal columns. */
    int rows;                       /**< Terminal rows. */

    /* Double-buffered screen output. */
    char  *screen_buf;              /**< ANSI escape buffer for rendering. */
    size_t screen_cap;              /**< Capacity of screen buffer. */
    size_t screen_len;              /**< Bytes written to screen buffer. */

    /* Connection state (set externally). */
    int server_fd;                  /**< TCP fd to editor server (-1 = none). */
    int client_fd;                  /**< TCP fd to client state (-1 = none). */

    /* Terminal mode saved for restore. */
    bool raw_mode;                  /**< Whether raw termios is active. */
} ctrl_tui_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize TUI state (does not enter raw mode).
 * @param tui  TUI context to initialize.
 * @return true on success.
 */
bool ctrl_tui_init(ctrl_tui_t *tui);

/**
 * @brief Free TUI resources (does not restore terminal).
 * @param tui  TUI context to destroy.
 */
void ctrl_tui_destroy(ctrl_tui_t *tui);

/* ------------------------------------------------------------------------ */
/* Input processing                                                          */
/* ------------------------------------------------------------------------ */

/**
 * @brief Feed a single byte of input to the TUI state machine.
 *
 * Handles mode transitions, command-line editing, and key dispatch.
 * Returns a command string to send to the server if Enter was pressed
 * in command mode, or NULL otherwise.
 *
 * @param tui  TUI context.
 * @param ch   Input character.
 * @return Command string to send (pointer into tui internals), or NULL.
 */
const char *ctrl_tui_feed_key(ctrl_tui_t *tui, char ch);

/* ------------------------------------------------------------------------ */
/* Rendering                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Render the TUI to the internal screen buffer.
 *
 * Builds ANSI escape sequences for the entire screen (status bar,
 * log area, command line) into screen_buf. Caller writes screen_buf
 * to stdout in a single write() call.
 *
 * @param tui  TUI context.
 */
void ctrl_tui_render(ctrl_tui_t *tui);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CTRL_TUI_H */
