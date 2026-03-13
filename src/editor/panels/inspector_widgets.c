/**
 * @file inspector_widgets.c
 * @brief Inspector float and vec3 widget implementations.
 *
 * Non-static functions: 4 (float_init, float_set, vec3_init,
 *                          vec3_set_component).
 */

#include "ferrum/editor/panels/inspector_widgets.h"
#include <string.h>
#include <stdio.h>

void inspector_float_widget_init(inspector_float_widget_t *w,
                                  const char *label, float initial,
                                  float min_val, float max_val) {
    snprintf(w->label, sizeof(w->label), "%s", label);
    w->min_value = min_val;
    w->max_value = max_val;
    w->value = initial;
}

void inspector_float_widget_set(inspector_float_widget_t *w, float value) {
    if (value < w->min_value) value = w->min_value;
    if (value > w->max_value) value = w->max_value;
    w->value = value;
}

void inspector_vec3_widget_init(inspector_vec3_widget_t *w,
                                 const char *label, const float initial[3]) {
    snprintf(w->label, sizeof(w->label), "%s", label);
    w->value[0] = initial[0];
    w->value[1] = initial[1];
    w->value[2] = initial[2];
}

void inspector_vec3_widget_set_component(inspector_vec3_widget_t *w,
                                          int component, float value) {
    if (component < 0 || component > 2) return;
    w->value[component] = value;
}
