/**
 * @file panel_toolbar.c
 * @brief Editor toolbar implementation.
 *
 * Non-static functions: 4 (init, destroy, set_transform, toggle_snap).
 */

#include "ferrum/editor/panels/panel_toolbar.h"
#include <string.h>
#include <stdio.h>

/** @brief Update button active states based on current transform mode. */
static void update_active_states_(editor_toolbar_t *tb) {
    for (uint32_t i = 0; i < tb->button_count; i++) {
        toolbar_button_t *btn = &tb->buttons[i];
        switch (btn->id) {
        case TOOLBAR_BTN_TRANSLATE:
            btn->active = (tb->active_transform == TOOLBAR_TRANSFORM_TRANSLATE);
            break;
        case TOOLBAR_BTN_ROTATE:
            btn->active = (tb->active_transform == TOOLBAR_TRANSFORM_ROTATE);
            break;
        case TOOLBAR_BTN_SCALE:
            btn->active = (tb->active_transform == TOOLBAR_TRANSFORM_SCALE);
            break;
        case TOOLBAR_BTN_SNAP:
            btn->active = tb->snap_enabled;
            break;
        default:
            break;
        }
    }
}

void editor_toolbar_init(editor_toolbar_t *tb) {
    memset(tb, 0, sizeof(*tb));
    tb->active_transform = TOOLBAR_TRANSFORM_TRANSLATE;
    tb->snap_enabled = false;

    /* Create default buttons. */
    tb->buttons[0] = (toolbar_button_t){
        .id = TOOLBAR_BTN_TRANSLATE, .label = "Move", .toggle = false};
    tb->buttons[1] = (toolbar_button_t){
        .id = TOOLBAR_BTN_ROTATE, .label = "Rotate", .toggle = false};
    tb->buttons[2] = (toolbar_button_t){
        .id = TOOLBAR_BTN_SCALE, .label = "Scale", .toggle = false};
    tb->buttons[3] = (toolbar_button_t){
        .id = TOOLBAR_BTN_SNAP, .label = "Snap", .toggle = true};
    tb->button_count = 4;

    update_active_states_(tb);
}

void editor_toolbar_destroy(editor_toolbar_t *tb) {
    (void)tb;
}

void editor_toolbar_set_transform(editor_toolbar_t *tb,
                                   toolbar_transform_mode_t mode) {
    tb->active_transform = mode;
    update_active_states_(tb);
}

void editor_toolbar_toggle_snap(editor_toolbar_t *tb) {
    tb->snap_enabled = !tb->snap_enabled;
    update_active_states_(tb);
}
