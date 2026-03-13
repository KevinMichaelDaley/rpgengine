/**
 * @file panel_toolbar.h
 * @brief Editor toolbar — transform mode buttons, snap toggle.
 *
 * Data model for toolbar button state. Rendering handled separately.
 *
 * Ownership: no dynamic allocation; all inline.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: out-of-range returns NULL.
 * Side effects: none.
 *
 * Public types: editor_toolbar_t, toolbar_button_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_PANELS_TOOLBAR_H
#define FERRUM_EDITOR_PANELS_TOOLBAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief Maximum toolbar buttons. */
#define TOOLBAR_MAX_BUTTONS 16
/** @brief Maximum button label length. */
#define TOOLBAR_LABEL_MAX 32

/**
 * @brief Toolbar transform mode.
 */
typedef enum toolbar_transform_mode {
    TOOLBAR_TRANSFORM_TRANSLATE = 0,
    TOOLBAR_TRANSFORM_ROTATE    = 1,
    TOOLBAR_TRANSFORM_SCALE     = 2
} toolbar_transform_mode_t;

/**
 * @brief Toolbar button identifiers.
 */
typedef enum toolbar_button_id {
    TOOLBAR_BTN_TRANSLATE = 0,
    TOOLBAR_BTN_ROTATE    = 1,
    TOOLBAR_BTN_SCALE     = 2,
    TOOLBAR_BTN_SNAP      = 3,
    TOOLBAR_BTN_COUNT
} toolbar_button_id_t;

/**
 * @brief A single toolbar button.
 */
typedef struct toolbar_button {
    toolbar_button_id_t id;                /**< Button identifier. */
    char                label[TOOLBAR_LABEL_MAX]; /**< Display label. */
    bool                active;            /**< Whether button is active/pressed. */
    bool                toggle;            /**< Whether button is a toggle. */
} toolbar_button_t;

/**
 * @brief Editor toolbar state.
 */
typedef struct editor_toolbar {
    toolbar_button_t         buttons[TOOLBAR_MAX_BUTTONS]; /**< Buttons. */
    uint32_t                 button_count;   /**< Number of buttons. */
    toolbar_transform_mode_t active_transform; /**< Active transform mode. */
    bool                     snap_enabled;   /**< Global snap toggle. */
} editor_toolbar_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize toolbar with default buttons.
 * @param tb  Toolbar (non-NULL).
 */
void editor_toolbar_init(editor_toolbar_t *tb);

/**
 * @brief Destroy toolbar (no-op, no dynamic allocation).
 * @param tb  Toolbar (non-NULL).
 */
void editor_toolbar_destroy(editor_toolbar_t *tb);

/* ---- Control ---- */

/**
 * @brief Set the active transform mode. Updates button active states.
 * @param tb    Toolbar (non-NULL).
 * @param mode  Transform mode.
 */
void editor_toolbar_set_transform(editor_toolbar_t *tb,
                                   toolbar_transform_mode_t mode);

/**
 * @brief Toggle the snap button on/off.
 * @param tb  Toolbar (non-NULL).
 */
void editor_toolbar_toggle_snap(editor_toolbar_t *tb);

/* ---- Query ---- */

/**
 * @brief Get a button by index.
 * @param tb     Toolbar (non-NULL).
 * @param index  Button index.
 * @return Pointer to button, or NULL if out of bounds.
 */
const toolbar_button_t *editor_toolbar_get_button(const editor_toolbar_t *tb,
                                                    uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_PANELS_TOOLBAR_H */
