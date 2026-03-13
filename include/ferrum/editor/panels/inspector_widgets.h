/**
 * @file inspector_widgets.h
 * @brief Inspector property widgets — float, vec3, dropdown, checkbox.
 *
 * Data models for inspector panel property editing. Each widget stores
 * its own label, value, and constraints. Rendering is handled separately.
 *
 * Ownership: no dynamic allocation; all stack/inline.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: out-of-range values are clamped.
 * Side effects: none.
 *
 * Public types: inspector_float_widget_t, inspector_vec3_widget_t (2-type rule).
 * Additional types in a separate section below.
 */
#ifndef FERRUM_EDITOR_PANELS_INSPECTOR_WIDGETS_H
#define FERRUM_EDITOR_PANELS_INSPECTOR_WIDGETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief Maximum label length for widgets. */
#define INSPECTOR_LABEL_MAX 64
/** @brief Maximum dropdown options. */
#define INSPECTOR_DROPDOWN_MAX_OPTIONS 32

/* ---- Float widget ---- */

/**
 * @brief Single float property widget with min/max clamping.
 */
typedef struct inspector_float_widget {
    char  label[INSPECTOR_LABEL_MAX]; /**< Display label. */
    float value;                       /**< Current value. */
    float min_value;                   /**< Minimum allowed value. */
    float max_value;                   /**< Maximum allowed value. */
} inspector_float_widget_t;

/**
 * @brief Initialize a float widget.
 * @param w          Widget (non-NULL).
 * @param label      Label string (non-NULL).
 * @param initial    Initial value.
 * @param min_val    Minimum value.
 * @param max_val    Maximum value.
 */
void inspector_float_widget_init(inspector_float_widget_t *w,
                                  const char *label, float initial,
                                  float min_val, float max_val);

/**
 * @brief Set the float widget value (clamped to [min, max]).
 * @param w      Widget (non-NULL).
 * @param value  New value.
 */
void inspector_float_widget_set(inspector_float_widget_t *w, float value);

/* ---- Vec3 widget ---- */

/**
 * @brief Three-component vector property widget.
 */
typedef struct inspector_vec3_widget {
    char  label[INSPECTOR_LABEL_MAX]; /**< Display label. */
    float value[3];                    /**< Current X, Y, Z values. */
} inspector_vec3_widget_t;

/**
 * @brief Initialize a vec3 widget.
 * @param w        Widget (non-NULL).
 * @param label    Label string (non-NULL).
 * @param initial  Initial values [3] (non-NULL).
 */
void inspector_vec3_widget_init(inspector_vec3_widget_t *w,
                                 const char *label, const float initial[3]);

/**
 * @brief Set a single component of the vec3 widget.
 * @param w          Widget (non-NULL).
 * @param component  Component index (0=X, 1=Y, 2=Z). Invalid = no-op.
 * @param value      New value.
 */
void inspector_vec3_widget_set_component(inspector_vec3_widget_t *w,
                                          int component, float value);

/* ---- Dropdown widget ---- */

/**
 * @brief Dropdown selection widget.
 */
typedef struct inspector_dropdown_widget {
    char        label[INSPECTOR_LABEL_MAX];
    const char *options[INSPECTOR_DROPDOWN_MAX_OPTIONS];
    uint32_t    option_count;
    uint32_t    selected_index;
} inspector_dropdown_widget_t;

/**
 * @brief Initialize a dropdown widget.
 * @param w         Widget (non-NULL).
 * @param label     Label (non-NULL).
 * @param options   Array of option strings.
 * @param count     Number of options.
 * @param initial   Initial selected index.
 */
void inspector_dropdown_widget_init(inspector_dropdown_widget_t *w,
                                     const char *label,
                                     const char *options[], uint32_t count,
                                     uint32_t initial);

/**
 * @brief Select a dropdown option by index. Clamped to valid range.
 * @param w      Widget (non-NULL).
 * @param index  Option index.
 */
void inspector_dropdown_widget_select(inspector_dropdown_widget_t *w,
                                       uint32_t index);

/* ---- Checkbox widget ---- */

/**
 * @brief Boolean checkbox widget.
 */
typedef struct inspector_checkbox_widget {
    char label[INSPECTOR_LABEL_MAX];
    bool checked;
} inspector_checkbox_widget_t;

/**
 * @brief Initialize a checkbox widget.
 * @param w        Widget (non-NULL).
 * @param label    Label (non-NULL).
 * @param initial  Initial checked state.
 */
void inspector_checkbox_widget_init(inspector_checkbox_widget_t *w,
                                     const char *label, bool initial);

/**
 * @brief Toggle the checkbox state.
 * @param w  Widget (non-NULL).
 */
void inspector_checkbox_widget_toggle(inspector_checkbox_widget_t *w);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_PANELS_INSPECTOR_WIDGETS_H */
