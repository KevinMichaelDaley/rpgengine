/**
 * @file scene_sync.h
 * @brief Scene editor sync state — offline queue, in-flight tracking, save.
 *
 * Implements Google Drive-style sync: tracks pending edits, queues
 * commands during disconnection, and replays on reconnect.
 *
 * Thread safety: single-threaded (scene editor main loop only).
 */
#ifndef FERRUM_EDITOR_SCENE_SYNC_H
#define FERRUM_EDITOR_SCENE_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Constants ---- */

#define SCENE_SYNC_DEFAULT_QUEUE_CAP 256
#define SCENE_SYNC_MAX_CMD_LEN       1024

/* ---- Types ---- */

/**
 * @brief Sync state enum.
 */
typedef enum scene_sync_state {
    SCENE_SYNC_IDLE    = 0, /**< All edits flushed and acked. */
    SCENE_SYNC_SYNCING = 1, /**< Commands in flight. */
    SCENE_SYNC_OFFLINE = 2, /**< Disconnected, edits queued locally. */
} scene_sync_state_t;

/**
 * @brief A single queued edit command.
 */
typedef struct scene_sync_entry {
    char     cmd[SCENE_SYNC_MAX_CMD_LEN]; /**< Command text. */
    uint32_t cmd_id;                       /**< Request ID. */
} scene_sync_entry_t;

/**
 * @brief Configuration for sync module.
 */
typedef struct scene_sync_config {
    uint32_t queue_capacity; /**< Offline queue capacity (0 = default). */
} scene_sync_config_t;

/**
 * @brief Sync state — tracks in-flight commands and offline queue.
 *
 * Ownership: init() allocates queue, destroy() frees.
 */
typedef struct scene_sync {
    scene_sync_state_t  state;             /**< Current sync state. */
    uint32_t            in_flight;         /**< Commands sent but not acked. */
    bool                force_save_pending;/**< :save force requested. */

    /* Offline edit queue (circular buffer) */
    scene_sync_entry_t *queue;             /**< Ring buffer of edits. */
    uint32_t            queue_capacity;    /**< Total queue slots. */
    uint32_t            queue_count;       /**< Entries currently queued. */
    uint32_t            queue_head;        /**< Next dequeue index. */
    uint32_t            queue_tail;        /**< Next enqueue index. */

    bool                initialized;       /**< True after init. */
} scene_sync_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize sync state.
 * @param sync    Sync state (must not be NULL).
 * @param config  Configuration (NULL for defaults).
 * @return true on success.
 */
bool scene_sync_init(scene_sync_t *sync, const scene_sync_config_t *config);

/**
 * @brief Destroy sync state and free queue.
 * @param sync  Sync state (may be NULL).
 */
void scene_sync_destroy(scene_sync_t *sync);

/* ---- Queue operations ---- */

/**
 * @brief Queue an edit command for later replay.
 * @param sync    Sync state.
 * @param cmd     Command text.
 * @param cmd_id  Request ID.
 * @return true if queued, false if queue is full.
 */
bool scene_sync_queue_edit(scene_sync_t *sync, const char *cmd,
                           uint32_t cmd_id);

/**
 * @brief Dequeue the next edit command (FIFO).
 * @param sync     Sync state.
 * @param buf      Output buffer for command text.
 * @param buf_cap  Buffer capacity.
 * @param cmd_id   Output: request ID.
 * @return true if an edit was dequeued, false if queue is empty.
 */
bool scene_sync_dequeue_edit(scene_sync_t *sync, char *buf, size_t buf_cap,
                             uint32_t *cmd_id);

/* ---- In-flight tracking ---- */

/**
 * @brief Mark a command as sent (in-flight).
 * @param sync  Sync state.
 */
void scene_sync_mark_sent(scene_sync_t *sync);

/**
 * @brief Mark a command as acknowledged.
 * @param sync  Sync state.
 */
void scene_sync_mark_acked(scene_sync_t *sync);

/* ---- State management ---- */

/**
 * @brief Update sync state based on in-flight count and queue.
 * @param sync  Sync state.
 */
void scene_sync_update_state(scene_sync_t *sync);

/**
 * @brief Request a forced save (:save force).
 * @param sync  Sync state.
 */
void scene_sync_save_force(scene_sync_t *sync);

/**
 * @brief Consume the force save flag (returns true once, then false).
 * @param sync  Sync state.
 * @return true if force save was pending.
 */
bool scene_sync_consume_force_save(scene_sync_t *sync);

/* ---- Status ---- */

/**
 * @brief Format sync status for TUI display.
 * @param sync  Sync state.
 * @param buf   Output buffer.
 * @param cap   Buffer capacity.
 */
void scene_sync_format_status(const scene_sync_t *sync, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_SYNC_H */
