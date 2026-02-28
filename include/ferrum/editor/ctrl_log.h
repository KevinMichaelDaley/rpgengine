/**
 * @file ctrl_log.h
 * @brief Controller log ring buffer.
 *
 * Stores a fixed-capacity ring of log entries for display in the TUI
 * log area. Supports scroll offset for viewing history.
 *
 * Thread safety: single-threaded (controller main loop only).
 */
#ifndef FERRUM_EDITOR_CTRL_LOG_H
#define FERRUM_EDITOR_CTRL_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

#define CTRL_LOG_MAX_TEXT  256
#define CTRL_LOG_DEFAULT_CAP  512

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Status indicator for command result display.
 */
typedef enum ctrl_log_status {
    CTRL_LOG_STATUS_NONE    = 0, /**< Not a command (no indicator). */
    CTRL_LOG_STATUS_PENDING = 1, /**< Command sent, awaiting response. */
    CTRL_LOG_STATUS_OK      = 2, /**< Command succeeded (green ✓). */
    CTRL_LOG_STATUS_FAIL    = 3, /**< Command failed (red ✗). */
} ctrl_log_status_t;

/**
 * @brief A single log entry.
 */
typedef struct ctrl_log_entry {
    char     text[CTRL_LOG_MAX_TEXT]; /**< Null-terminated log message. */
    uint8_t  level;                  /**< 0=info, 1=warn, 2=error. */
    uint8_t  status;                 /**< ctrl_log_status_t indicator. */
    uint32_t cmd_id;                 /**< Request ID (for matching responses). */
} ctrl_log_entry_t;

/**
 * @brief Ring buffer of log entries.
 *
 * Entries are written sequentially; when the ring wraps, oldest entries
 * are overwritten. Scroll offset allows viewing history.
 *
 * Ownership: init() allocates, destroy() frees.
 */
typedef struct ctrl_log {
    ctrl_log_entry_t *entries;   /**< Ring buffer of entries. */
    uint32_t          capacity;  /**< Total ring slots. */
    uint32_t          count;     /**< Total entries ever written (monotonic). */
    uint32_t          scroll;    /**< Lines scrolled up from bottom (0 = latest). */
} ctrl_log_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the log buffer.
 * @param log       Log to initialize.
 * @param capacity  Number of entries (0 = default 512).
 * @return true on success.
 */
bool ctrl_log_init(ctrl_log_t *log, uint32_t capacity);

/**
 * @brief Free log memory.
 * @param log  Log to destroy.
 */
void ctrl_log_destroy(ctrl_log_t *log);

/* ------------------------------------------------------------------------ */
/* Mutation                                                                  */
/* ------------------------------------------------------------------------ */

/**
 * @brief Add a log entry.
 * @param log    Log buffer.
 * @param level  Log level (0=info, 1=warn, 2=error).
 * @param text   Message text (truncated to CTRL_LOG_MAX_TEXT-1).
 */
void ctrl_log_add(ctrl_log_t *log, uint8_t level, const char *text);

/**
 * @brief Add a log entry with a command ID and pending status.
 * @param log     Log buffer.
 * @param text    Message text.
 * @param cmd_id  Request ID for matching responses.
 */
void ctrl_log_add_cmd(ctrl_log_t *log, const char *text, uint32_t cmd_id);

/**
 * @brief Update the status of a pending command entry by ID.
 *
 * Searches backwards from newest for an entry with matching cmd_id.
 *
 * @param log     Log buffer.
 * @param cmd_id  Request ID to find.
 * @param status  New status (CTRL_LOG_STATUS_OK or CTRL_LOG_STATUS_FAIL).
 * @return true if found and updated.
 */
bool ctrl_log_set_cmd_status(ctrl_log_t *log, uint32_t cmd_id,
                             ctrl_log_status_t status);

/**
 * @brief Clear all log entries.
 * @param log  Log buffer.
 */
void ctrl_log_clear(ctrl_log_t *log);

/* ------------------------------------------------------------------------ */
/* Query                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Get a log entry by index from the bottom (0 = newest).
 *
 * Takes scroll offset into account: index 0 with scroll=0 returns
 * the most recent entry; with scroll=3, it returns 4th from newest.
 *
 * @param log    Log buffer.
 * @param index  Visual row from bottom (0 = bottom row).
 * @return Pointer to entry, or NULL if out of range.
 */
const ctrl_log_entry_t *ctrl_log_get(const ctrl_log_t *log, uint32_t index);

/**
 * @brief Get total number of log entries (capped at capacity).
 * @param log  Log buffer.
 * @return Number of available entries.
 */
uint32_t ctrl_log_visible_count(const ctrl_log_t *log);

/**
 * @brief Scroll up (into history).
 * @param log    Log buffer.
 * @param lines  Number of lines to scroll.
 */
void ctrl_log_scroll_up(ctrl_log_t *log, uint32_t lines);

/**
 * @brief Scroll down (toward present).
 * @param log    Log buffer.
 * @param lines  Number of lines to scroll.
 */
void ctrl_log_scroll_down(ctrl_log_t *log, uint32_t lines);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CTRL_LOG_H */
