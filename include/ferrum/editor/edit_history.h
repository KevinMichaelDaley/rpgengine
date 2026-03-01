/**
 * @file edit_history.h
 * @brief Edit history — global command log for level reconstruction.
 *
 * Records every dispatched command with full context: command name,
 * arguments, result, cursor position, selection, and active aliases.
 * Entries are flushed to a newline-delimited JSON file (JSONL) for
 * git-friendly tracking and level reconstruction.
 *
 * Thread safety: not thread-safe; call from tick thread only.
 */
#ifndef FERRUM_EDITOR_EDIT_HISTORY_H
#define FERRUM_EDITOR_EDIT_HISTORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Forward declarations. */
struct edit_cmd_ctx;

/* ------------------------------------------------------------------------ */
/* Configuration                                                             */
/* ------------------------------------------------------------------------ */

/** @brief Maximum number of entries in the in-memory ring buffer. */
#define EDIT_HISTORY_RING_CAP  4096

/** @brief Maximum length of the raw JSON strings stored per entry. */
#define EDIT_HISTORY_CMD_MAX    64
#define EDIT_HISTORY_ARGS_MAX  2048
#define EDIT_HISTORY_RESULT_MAX 2048
#define EDIT_HISTORY_CTX_MAX   4096

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief A single history entry — one dispatched command.
 *
 * Stores the command name, raw args JSON, result JSON, context snapshot,
 * success flag, sequence number, and timestamp.
 */
typedef struct edit_history_entry {
    uint64_t seq;                          /**< Monotonic sequence number. */
    char     timestamp[32];                /**< ISO-8601 UTC timestamp. */
    char     cmd[EDIT_HISTORY_CMD_MAX];    /**< Command name. */
    char     args[EDIT_HISTORY_ARGS_MAX];  /**< Raw args JSON string. */
    char     result[EDIT_HISTORY_RESULT_MAX]; /**< Result JSON string. */
    char     ctx[EDIT_HISTORY_CTX_MAX];    /**< Context snapshot JSON. */
    bool     ok;                           /**< true = success. */
} edit_history_entry_t;

/**
 * @brief Edit history — ring buffer of entries with file-backed log.
 *
 * Ownership:
 * - Owns the ring buffer (heap-allocated).
 * - Owns the FILE* for the log file (opened on init, closed on destroy).
 * - Does NOT own the edit_cmd_ctx (borrowed pointer).
 */
typedef struct edit_history {
    edit_history_entry_t *entries;  /**< Ring buffer of entries. */
    uint32_t capacity;             /**< Ring buffer capacity. */
    uint32_t head;                 /**< Next write position. */
    uint32_t flush_cursor;         /**< Next entry to flush to file. */
    uint64_t seq;                  /**< Next sequence number. */
    FILE    *log_file;             /**< Log file handle (NULL = no file). */
} edit_history_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the history ring buffer and open the log file.
 *
 * @param history   History context to initialize.
 * @param capacity  Number of entries in the ring buffer (0 = default 4096).
 * @param log_path  Path to the JSONL log file (NULL = no file logging).
 * @return true on success, false on allocation or file open failure.
 *
 * Ownership: caller owns the history and must call edit_history_destroy().
 * Side effects: creates/appends to the log file at log_path.
 */
bool edit_history_init(edit_history_t *history, uint32_t capacity,
                       const char *log_path);

/**
 * @brief Destroy the history, flushing pending entries and closing the file.
 *
 * @param history  History context.
 */
void edit_history_destroy(edit_history_t *history);

/* ------------------------------------------------------------------------ */
/* Recording                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Record a command execution into the history.
 *
 * Captures the command name, raw args JSON, result JSON, success flag,
 * current timestamp, and a context snapshot (cursor, selection, aliases).
 *
 * @param history   History context.
 * @param ctx       Editor command context (for cursor/selection/alias snapshot).
 *                  May be NULL (context fields will be empty).
 * @param cmd       Command name (NUL-terminated).
 * @param args_json Raw args JSON string (NUL-terminated, may be NULL).
 * @param result_json Raw result JSON string (NUL-terminated, may be NULL).
 * @param ok        Whether the command succeeded.
 *
 * Side effects: advances the ring head; may overwrite oldest entry.
 */
void edit_history_record(edit_history_t *history,
                         const struct edit_cmd_ctx *ctx,
                         const char *cmd,
                         const char *args_json,
                         const char *result_json,
                         bool ok);

/* ------------------------------------------------------------------------ */
/* Flushing                                                                  */
/* ------------------------------------------------------------------------ */

/**
 * @brief Flush all pending (unwritten) entries to the log file.
 *
 * @param history  History context.
 * @return Number of entries flushed.
 *
 * Side effects: writes to log file and calls fflush(). Advances flush_cursor.
 * If log_file is NULL, returns 0.
 */
uint32_t edit_history_flush(edit_history_t *history);

/* ------------------------------------------------------------------------ */
/* Context snapshot                                                          */
/* ------------------------------------------------------------------------ */

/**
 * @brief Build a JSON context snapshot string.
 *
 * Captures cursor position, selected entity IDs, and active @ aliases.
 *
 * @param ctx  Editor command context.
 * @param buf  Output buffer.
 * @param cap  Buffer capacity.
 * @return Bytes written (excluding NUL), 0 on error.
 */
size_t edit_history_snapshot_ctx(const struct edit_cmd_ctx *ctx,
                                char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_HISTORY_H */
