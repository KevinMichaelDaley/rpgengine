/**
 * @file panel_toolbar_query.c
 * @brief Toolbar query function.
 *
 * Non-static functions: 1 (get_button).
 */

#include "ferrum/editor/panels/panel_toolbar.h"
#include <stddef.h>

const toolbar_button_t *editor_toolbar_get_button(const editor_toolbar_t *tb,
                                                    uint32_t index) {
    if (index >= tb->button_count) return NULL;
    return &tb->buttons[index];
}
