/**
 * @file ctrl_mesh_mode.h
 * @brief Mesh mode state machine for TUI keybinding dispatch.
 *
 * Types: ctrl_mesh_mode_t (mesh mode state).
 *
 * When the editor enters mesh editing, ctrl_mesh_mode_t tracks the
 * current selection mode, display flags (wireframe, xray), and
 * selection statistics. Keybindings are mode-dependent:
 *
 *   Face mode:  e=extrude, i=inset, g=grow, G=shrink
 *   Edge mode:  b=bevel, c=loop_cut, x=edge_ring, l=edge_loop
 *   All modes:  Tab=wireframe, ~=xray, u=unwrap, 1-5=mode switch
 *
 * Ownership: no heap allocation.
 * Nullability: NULL pointers handled gracefully.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_CTRL_MESH_MODE_H
#define FERRUM_EDITOR_CTRL_MESH_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_edit.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Mesh mode state: selection mode, display flags, statistics.
 */
typedef struct ctrl_mesh_mode {
    mesh_sel_mode_t sel_mode;   /**< Current selection topology. */
    uint32_t sel_count;         /**< Number of selected elements. */
    uint32_t vertex_count;      /**< Mesh vertex count. */
    uint32_t tri_count;         /**< Mesh triangle count. */
    bool     wireframe;         /**< Wireframe overlay toggle. */
    bool     xray;              /**< X-ray (see-through) selection toggle. */
    bool     active;            /**< Whether mesh mode is active. */
} ctrl_mesh_mode_t;

/* ------------------------------------------------------------------ */
/* Lifecycle (ctrl_mesh_mode.c)                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize mesh mode to defaults (vertex mode, all flags off).
 * @param mm  Mesh mode state. Not NULL.
 */
void ctrl_mesh_mode_init(ctrl_mesh_mode_t *mm);

/**
 * @brief Switch selection mode.
 * @param mm    Mesh mode state. Not NULL.
 * @param mode  New selection mode.
 */
void ctrl_mesh_mode_set_sel_mode(ctrl_mesh_mode_t *mm, mesh_sel_mode_t mode);

/* ------------------------------------------------------------------ */
/* Key dispatch (ctrl_mesh_mode_keys.c)                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Map a key to a command string based on current mode.
 *
 * Returns NULL if the key has no binding in the current mode.
 *
 * @param mm   Mesh mode state. NULL returns NULL.
 * @param key  Key character (ASCII).
 * @return Command name string, or NULL if unbound.
 */
const char *ctrl_mesh_mode_key_to_command(const ctrl_mesh_mode_t *mm, char key);

/* ------------------------------------------------------------------ */
/* Display (ctrl_mesh_mode_status.c)                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Generate status bar text for mesh mode.
 *
 * @param mm       Mesh mode state. NULL writes empty string.
 * @param buf      Output buffer.
 * @param buf_size Buffer capacity.
 */
void ctrl_mesh_mode_status(const ctrl_mesh_mode_t *mm, char *buf, size_t buf_size);

/**
 * @brief Toggle wireframe display flag.
 * @param mm  Mesh mode state. NULL is no-op.
 */
void ctrl_mesh_mode_toggle_wireframe(ctrl_mesh_mode_t *mm);

/**
 * @brief Toggle x-ray selection flag.
 * @param mm  Mesh mode state. NULL is no-op.
 */
void ctrl_mesh_mode_toggle_xray(ctrl_mesh_mode_t *mm);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CTRL_MESH_MODE_H */
