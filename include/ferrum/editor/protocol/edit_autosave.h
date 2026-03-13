/**
 * @file edit_autosave.h
 * @brief Server-side autosave — periodic and forced world persistence.
 *
 * Manages autosave timing and force-save requests. The actual file I/O
 * is delegated to edit_serialize.h. This module tracks dirty state,
 * interval timing, and the :save force flag.
 *
 * Thread safety: single-threaded (server tick thread only).
 */
#ifndef FERRUM_EDITOR_PROTOCOL_EDIT_AUTOSAVE_H
#define FERRUM_EDITOR_PROTOCOL_EDIT_AUTOSAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ---- Constants ---- */

#define EDIT_AUTOSAVE_DEFAULT_INTERVAL_MS 30000  /**< 30 seconds. */
#define EDIT_AUTOSAVE_MAX_PATH            512

/* ---- Types ---- */

/**
 * @brief Autosave configuration.
 */
typedef struct edit_autosave_config {
    uint32_t    interval_ms; /**< Save interval in milliseconds (0 = default). */
    const char *save_path;   /**< Path to save file (must not be NULL). */
} edit_autosave_config_t;

/**
 * @brief Autosave state.
 *
 * Ownership: init() sets up state, destroy() cleans up.
 */
typedef struct edit_autosave {
    uint32_t interval_ms;     /**< Save interval. */
    uint64_t last_save_ms;    /**< Timestamp of last save (monotonic ms). */
    bool     dirty;           /**< True if world modified since last save. */
    bool     force_pending;   /**< True if :save force requested. */
    char     save_path[EDIT_AUTOSAVE_MAX_PATH]; /**< Save file path. */
    bool     initialized;     /**< True after init. */
} edit_autosave_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize autosave state.
 * @param autosave  State to initialize (must not be NULL).
 * @param config    Configuration (must not be NULL, save_path must not be NULL).
 * @return true on success.
 */
bool edit_autosave_init(edit_autosave_t *autosave,
                        const edit_autosave_config_t *config);

/**
 * @brief Destroy autosave state. Safe on NULL/double-call.
 * @param autosave  State to destroy.
 */
void edit_autosave_destroy(edit_autosave_t *autosave);

/* ---- State management ---- */

/**
 * @brief Mark the world as dirty (modified since last save).
 * @param autosave  Autosave state.
 */
void edit_autosave_mark_dirty(edit_autosave_t *autosave);

/**
 * @brief Request an immediate forced save.
 * @param autosave  Autosave state.
 */
void edit_autosave_request_force(edit_autosave_t *autosave);

/**
 * @brief Check if a save should occur now.
 *
 * Returns true if: force is pending, OR (dirty AND interval expired).
 *
 * @param autosave   Autosave state.
 * @param now_ms     Current monotonic time in milliseconds.
 * @return true if save should happen.
 */
bool edit_autosave_should_save(const edit_autosave_t *autosave, uint64_t now_ms);

/**
 * @brief Notify that a save completed successfully.
 *
 * Clears dirty and force_pending flags, updates last_save_ms.
 *
 * @param autosave  Autosave state.
 * @param now_ms    Current monotonic time in milliseconds.
 */
void edit_autosave_did_save(edit_autosave_t *autosave, uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_PROTOCOL_EDIT_AUTOSAVE_H */
