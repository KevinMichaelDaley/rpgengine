/**
 * @file ctrl_mesh_mode_status.c
 * @brief Status bar text generation for mesh mode.
 */
#include "ferrum/editor/ctrl_mesh_mode.h"
#include <stdio.h>
#include <string.h>

/** Selection mode names. */
static const char *mode_names_[] = {
    "VERTEX", "EDGE", "FACE", "POLYGROUP", "OBJECT"
};

void ctrl_mesh_mode_status(const ctrl_mesh_mode_t *mm, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    buf[0] = '\0';
    if (!mm) return;

    const char *mode_name = "???";
    if ((int)mm->sel_mode >= 0 && (int)mm->sel_mode < MESH_SEL_MODE_COUNT) {
        mode_name = mode_names_[(int)mm->sel_mode];
    }

    snprintf(buf, buf_size,
             "[%s] %u selected | V:%u T:%u%s%s",
             mode_name,
             mm->sel_count,
             mm->vertex_count,
             mm->tri_count,
             mm->wireframe ? " [WIRE]" : "",
             mm->xray ? " [XRAY]" : "");
}
