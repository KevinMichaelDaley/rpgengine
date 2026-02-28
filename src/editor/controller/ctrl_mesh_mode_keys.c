/**
 * @file ctrl_mesh_mode_keys.c
 * @brief Mode-dependent key-to-command dispatch for mesh editing.
 */
#include "ferrum/editor/ctrl_mesh_mode.h"
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Key binding tables                                                  */
/* ------------------------------------------------------------------ */

typedef struct key_binding {
    char        key;
    const char *command;
} key_binding_t;

/** Face-mode keybindings. */
static const key_binding_t face_keys_[] = {
    { 'e', "extrude" },
    { 'i', "inset" },
    { 'g', "grow_selection" },
    { 'G', "shrink_selection" },
    { 0, NULL }
};

/** Edge-mode keybindings. */
static const key_binding_t edge_keys_[] = {
    { 'b', "bevel" },
    { 'c', "loop_cut" },
    { 'x', "edge_ring" },
    { 'l', "edge_loop" },
    { 0, NULL }
};

/** Lookup key in a binding table. */
static const char *lookup_(const key_binding_t *table, char key) {
    for (int i = 0; table[i].command != NULL; i++) {
        if (table[i].key == key) return table[i].command;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

const char *ctrl_mesh_mode_key_to_command(const ctrl_mesh_mode_t *mm, char key) {
    if (!mm) return NULL;

    switch (mm->sel_mode) {
    case MESH_SEL_MODE_FACE:
    case MESH_SEL_MODE_POLYGROUP:
        return lookup_(face_keys_, key);
    case MESH_SEL_MODE_EDGE:
        return lookup_(edge_keys_, key);
    default:
        return NULL;
    }
}
