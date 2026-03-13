/**
 * @file mode_manager.c
 * @brief Editor mode manager implementation.
 *
 * Non-static functions: 4 (init, destroy, switch, active_name).
 */

#include "ferrum/editor/mode/mode_manager.h"
#include <string.h>

void mode_manager_init(mode_manager_t *mgr) {
    memset(mgr, 0, sizeof(*mgr));

    /* Register Object mode. */
    mgr->modes[0] = (editor_mode_vtable_t){
        .name = "object",
        .on_enter = NULL,
        .on_exit = NULL,
    };
    mgr->mode_count = 1;
    mgr->active_mode = EDITOR_MODE_OBJECT;
}

void mode_manager_destroy(mode_manager_t *mgr) {
    /* Call exit on active mode. */
    if (mgr->active_mode < mgr->mode_count &&
        mgr->modes[mgr->active_mode].on_exit) {
        mgr->modes[mgr->active_mode].on_exit(mgr->user_data);
    }
    memset(mgr, 0, sizeof(*mgr));
}

void mode_manager_switch(mode_manager_t *mgr, editor_mode_id_t mode) {
    if ((uint32_t)mode >= mgr->mode_count) return;
    if (mode == mgr->active_mode) return;

    /* Exit current mode. */
    if (mgr->modes[mgr->active_mode].on_exit) {
        mgr->modes[mgr->active_mode].on_exit(mgr->user_data);
    }

    mgr->active_mode = mode;

    /* Enter new mode. */
    if (mgr->modes[mode].on_enter) {
        mgr->modes[mode].on_enter(mgr->user_data);
    }
}

const char *mode_manager_active_name(const mode_manager_t *mgr) {
    if ((uint32_t)mgr->active_mode < mgr->mode_count) {
        return mgr->modes[mgr->active_mode].name;
    }
    return "unknown";
}
