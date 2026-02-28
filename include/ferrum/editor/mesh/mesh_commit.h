/**
 * @file mesh_commit.h
 * @brief Bake editable mesh (mesh_slot_t) into a world entity + FVMA asset.
 *
 * Types: mesh_commit_args_t, mesh_commit_result_t.
 *
 * mesh_commit serializes the mesh to FVMA binary, creates a new entity
 * of type EDIT_ENTITY_TYPE_MESH, positions it at the mesh bounding box
 * center, and optionally applies a material override.
 *
 * Ownership: result.fvma_data is heap-allocated; caller must free().
 * Nullability: NULL pointers return false.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_COMMIT_H
#define FERRUM_EDITOR_MESH_COMMIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/edit_entity.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Arguments for mesh_commit.
 */
typedef struct mesh_commit_args {
    char     entity_name[256];      /**< Entity display name. */
    char     material_override[256]; /**< Material path (empty = none). */
    bool     clear_slot;            /**< If true, clear mesh after commit. */
} mesh_commit_args_t;

/**
 * @brief Result of a successful mesh_commit.
 */
typedef struct mesh_commit_result {
    uint32_t entity_id;   /**< Created entity ID. */
    uint8_t *fvma_data;   /**< Heap-allocated FVMA binary. Caller frees. */
    size_t   fvma_size;   /**< FVMA binary size in bytes. */
} mesh_commit_result_t;

/* ------------------------------------------------------------------ */
/* API (mesh_commit.c)                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Bake a mesh slot into a world entity.
 *
 * Serializes the mesh to FVMA format, creates an entity, and positions
 * it at the mesh bounding box center.
 *
 * @param slot   Mesh to commit. Not NULL.
 * @param store  Entity store. Not NULL.
 * @param args   Commit arguments. Not NULL.
 * @param result Output result. Not NULL.
 * @return true on success.
 */
bool mesh_commit(mesh_slot_t *slot,
                 edit_entity_store_t *store,
                 const mesh_commit_args_t *args,
                 mesh_commit_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_COMMIT_H */
