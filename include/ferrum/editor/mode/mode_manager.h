/**
 * @file mode_manager.h
 * @brief Editor mode manager — vtable dispatch for editor modes.
 *
 * Manages the active editor mode (Object, Mesh, etc.) and provides
 * mode switching with enter/exit callbacks.
 *
 * Ownership: init() sets up internal state; destroy() cleans up.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: invalid mode IDs are silently ignored.
 * Side effects: mode switch calls exit on old mode, enter on new.
 *
 * Public types: mode_manager_t, editor_mode_id_t (enum) (2-type rule).
 */
#ifndef FERRUM_EDITOR_MODE_MANAGER_H
#define FERRUM_EDITOR_MODE_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief Maximum registered modes. */
#define EDITOR_MODE_MAX 8

/**
 * @brief Editor mode identifiers.
 */
typedef enum editor_mode_id {
    EDITOR_MODE_OBJECT = 0,
    EDITOR_MODE_COUNT
} editor_mode_id_t;

/**
 * @brief Mode vtable — callbacks for mode lifecycle.
 */
typedef struct editor_mode_vtable {
    const char *name;                     /**< Mode name (e.g., "object"). */
    void (*on_enter)(void *user_data);    /**< Called when mode is activated. */
    void (*on_exit)(void *user_data);     /**< Called when mode is deactivated. */
} editor_mode_vtable_t;

/**
 * @brief Mode manager state.
 */
typedef struct mode_manager {
    editor_mode_vtable_t modes[EDITOR_MODE_MAX]; /**< Registered modes. */
    uint32_t             mode_count;              /**< Number of registered modes. */
    editor_mode_id_t     active_mode;             /**< Currently active mode. */
    void                *user_data;               /**< Opaque context for callbacks. */
} mode_manager_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize the mode manager with default modes (Object).
 * @param mgr  Manager to initialize (non-NULL).
 */
void mode_manager_init(mode_manager_t *mgr);

/**
 * @brief Destroy the mode manager.
 * @param mgr  Manager to destroy (non-NULL).
 */
void mode_manager_destroy(mode_manager_t *mgr);

/* ---- Control ---- */

/**
 * @brief Switch to a different editor mode.
 *
 * Calls on_exit for the current mode and on_enter for the new mode.
 * No-op if switching to the same mode.
 *
 * @param mgr   Manager (non-NULL).
 * @param mode  Target mode ID.
 */
void mode_manager_switch(mode_manager_t *mgr, editor_mode_id_t mode);

/**
 * @brief Get the name of the currently active mode.
 * @param mgr  Manager (non-NULL).
 * @return Mode name string, or "unknown".
 */
const char *mode_manager_active_name(const mode_manager_t *mgr);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MODE_MANAGER_H */
