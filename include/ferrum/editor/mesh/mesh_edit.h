/**
 * @file mesh_edit.h
 * @brief Top-level mesh editing subsystem.
 *
 * mesh_edit_t manages an array of MESH_MAX_EDITABLE mesh slots,
 * the active slot index, the current selection mode, and per-element
 * selection bitsets (vertex, edge, face).
 *
 * Ownership: init() allocates, destroy() frees.
 * Nullability: NULL edit pointer handled gracefully.
 * Thread safety: not thread-safe — single-thread access only.
 */
#ifndef FERRUM_EDITOR_MESH_EDIT_H
#define FERRUM_EDITOR_MESH_EDIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/editor/mesh/mesh_slot.h"

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** @brief Maximum number of simultaneously editable mesh slots. */
#define MESH_MAX_EDITABLE  16

/* ------------------------------------------------------------------------ */
/* Selection mode enum                                                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief Mesh element selection mode.
 */
typedef enum mesh_sel_mode {
    MESH_SEL_MODE_VERTEX    = 0, /**< Select individual vertices. */
    MESH_SEL_MODE_EDGE      = 1, /**< Select edges (vertex pairs). */
    MESH_SEL_MODE_FACE      = 2, /**< Select individual triangles. */
    MESH_SEL_MODE_POLYGROUP = 3, /**< Select face groups. */
    MESH_SEL_MODE_OBJECT    = 4, /**< Select entire mesh. */
    MESH_SEL_MODE_COUNT     = 5
} mesh_sel_mode_t;

/* ------------------------------------------------------------------------ */
/* Selection bitset                                                          */
/* ------------------------------------------------------------------------ */

/**
 * @brief Dynamic bitset for mesh element selection.
 *
 * Tracks which elements (vertices, edges, or faces) are selected.
 * Grows on demand; maintains a count of set bits.
 */
typedef struct mesh_sel_bitset {
    uint64_t *bits;      /**< Heap-allocated bit array. */
    uint32_t  capacity;  /**< Number of uint64_t words allocated. */
    uint32_t  count;     /**< Number of set bits (cached). */
} mesh_sel_bitset_t;

/* Bitset lifecycle and operations (mesh_sel_bitset.c) */

/** @brief Initialize an empty bitset. */
void mesh_sel_bitset_init(mesh_sel_bitset_t *bs);

/** @brief Free bitset memory. NULL-safe. */
void mesh_sel_bitset_destroy(mesh_sel_bitset_t *bs);

/** @brief Set bit at index (select element). Grows if needed. */
void mesh_sel_bitset_set(mesh_sel_bitset_t *bs, uint32_t index);

/** @brief Clear bit at index (deselect element). */
void mesh_sel_bitset_unset(mesh_sel_bitset_t *bs, uint32_t index);

/** @brief Toggle bit at index. */
void mesh_sel_bitset_toggle(mesh_sel_bitset_t *bs, uint32_t index);

/** @brief Test if bit is set. Returns false for out-of-range. */
bool mesh_sel_bitset_test(const mesh_sel_bitset_t *bs, uint32_t index);

/** @brief Clear all bits, reset count to 0. */
void mesh_sel_bitset_clear_all(mesh_sel_bitset_t *bs);

/* ------------------------------------------------------------------------ */
/* mesh_edit_t                                                               */
/* ------------------------------------------------------------------------ */

/**
 * @brief Top-level mesh editing subsystem.
 */
typedef struct mesh_edit {
    mesh_slot_t       slots[MESH_MAX_EDITABLE]; /**< Editable mesh slots. */
    uint32_t          active_slot;               /**< Currently active slot index. */
    mesh_sel_mode_t   mode;                      /**< Current selection mode. */
    mesh_sel_bitset_t sel_vertices;              /**< Selected vertex indices. */
    mesh_sel_bitset_t sel_edges;                 /**< Selected edge indices. */
    mesh_sel_bitset_t sel_faces;                 /**< Selected face indices. */
} mesh_edit_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle (mesh_edit.c)                                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the mesh edit subsystem.
 *
 * Initializes all slots (empty), sets active_slot=0, mode=FACE.
 *
 * @param edit  Subsystem to initialize. Must not be NULL.
 * @return true on success, false on NULL.
 */
bool mesh_edit_init(mesh_edit_t *edit);

/**
 * @brief Destroy the mesh edit subsystem and all slots.
 *
 * @param edit  Subsystem to destroy. NULL is a safe no-op.
 */
void mesh_edit_destroy(mesh_edit_t *edit);

/* ------------------------------------------------------------------------ */
/* Slot access (mesh_edit_slot.c)                                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Set the active slot index.
 *
 * @param edit  Subsystem. Must not be NULL.
 * @param idx   Slot index [0, MESH_MAX_EDITABLE).
 * @return true on success, false if out of range.
 */
bool mesh_edit_set_active_slot(mesh_edit_t *edit, uint32_t idx);

/**
 * @brief Get a mutable pointer to the active slot.
 *
 * @param edit  Subsystem. NULL returns NULL.
 * @return Pointer to active mesh_slot_t, or NULL.
 */
mesh_slot_t *mesh_edit_get_active_slot(mesh_edit_t *edit);

/* ------------------------------------------------------------------------ */
/* Mode (mesh_edit_mode.c)                                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief Set the selection mode. Clears all selections.
 *
 * @param edit  Subsystem. Must not be NULL.
 * @param mode  New mode. Must be < MESH_SEL_MODE_COUNT.
 * @return true on success, false if invalid mode.
 */
bool mesh_edit_set_mode(mesh_edit_t *edit, mesh_sel_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_EDIT_H */
