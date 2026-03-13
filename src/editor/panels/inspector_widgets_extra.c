/**
 * @file inspector_widgets_extra.c
 * @brief Inspector dropdown and checkbox widget implementations.
 *
 * Non-static functions: 4 (dropdown_init, dropdown_select,
 *                          checkbox_init, checkbox_toggle).
 */

#include "ferrum/editor/panels/inspector_widgets.h"
#include <string.h>
#include <stdio.h>

void inspector_dropdown_widget_init(inspector_dropdown_widget_t *w,
                                     const char *label,
                                     const char *options[], uint32_t count,
                                     uint32_t initial) {
    snprintf(w->label, sizeof(w->label), "%s", label);
    w->option_count = count;
    if (count > INSPECTOR_DROPDOWN_MAX_OPTIONS) {
        w->option_count = INSPECTOR_DROPDOWN_MAX_OPTIONS;
    }
    for (uint32_t i = 0; i < w->option_count; i++) {
        w->options[i] = options[i];
    }
    w->selected_index = (initial < w->option_count) ? initial : 0;
}

void inspector_dropdown_widget_select(inspector_dropdown_widget_t *w,
                                       uint32_t index) {
    if (index >= w->option_count) return;
    w->selected_index = index;
}

void inspector_checkbox_widget_init(inspector_checkbox_widget_t *w,
                                     const char *label, bool initial) {
    snprintf(w->label, sizeof(w->label), "%s", label);
    w->checked = initial;
}

void inspector_checkbox_widget_toggle(inspector_checkbox_widget_t *w) {
    w->checked = !w->checked;
}
