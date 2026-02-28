/**
 * @file ctrl_mesh_mode.c
 * @brief Mesh mode state lifecycle and toggle operations.
 */
#include "ferrum/editor/ctrl_mesh_mode.h"
#include <string.h>

void ctrl_mesh_mode_init(ctrl_mesh_mode_t *mm) {
    if (!mm) return;
    memset(mm, 0, sizeof(*mm));
    mm->sel_mode = MESH_SEL_MODE_VERTEX;
}

void ctrl_mesh_mode_set_sel_mode(ctrl_mesh_mode_t *mm, mesh_sel_mode_t mode) {
    if (!mm) return;
    if ((int)mode >= 0 && (int)mode < MESH_SEL_MODE_COUNT) {
        mm->sel_mode = mode;
    }
}

void ctrl_mesh_mode_toggle_wireframe(ctrl_mesh_mode_t *mm) {
    if (!mm) return;
    mm->wireframe = !mm->wireframe;
}

void ctrl_mesh_mode_toggle_xray(ctrl_mesh_mode_t *mm) {
    if (!mm) return;
    mm->xray = !mm->xray;
}
