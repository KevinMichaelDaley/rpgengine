#ifndef FERRUM_RENDERER_MESH_REGISTRY_H
#define FERRUM_RENDERER_MESH_REGISTRY_H

/**
 * @file mesh_registry.h
 * @brief Central mesh store mapping opaque handles to loaded mesh data.
 *
 * The registry holds both static and skeletal meshes in a single flat
 * array of union slots.  Handles embed an index and a generation counter
 * so stale references are detected cheaply.
 *
 * Capacity is configurable at init time.  Internal arrays are allocated
 * with a single malloc and freed on destroy.
 *
 * @note Ownership: the registry owns all inserted meshes.
 *       mesh_registry_destroy() destroys every live mesh.
 * @note Nullability: all pointer parameters must be non-NULL unless
 *       documented otherwise.
 * @note Error semantics: functions return MESH_REGISTRY_OK on success
 *       or an error code.  On error, output is left unmodified.
 * @note Thread safety: not thread-safe.  External synchronization
 *       is required for concurrent access.
 */

#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/mesh/mesh_handle.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Status codes ─────────────────────────────────────────────────── */

/** @brief Status codes for mesh registry operations. */
enum {
    MESH_REGISTRY_OK          = 0, /**< Success. */
    MESH_REGISTRY_ERR_INVALID = 1, /**< NULL or invalid parameter. */
    MESH_REGISTRY_ERR_FULL    = 2, /**< No free slots remaining. */
    MESH_REGISTRY_ERR_GL      = 3, /**< GL resource creation failed. */
    MESH_REGISTRY_ERR_OOM     = 4  /**< Allocation failure. */
};

/* ── Registry type ────────────────────────────────────────────────── */

/**
 * @brief Central mesh store with handle-based lookup.
 *
 * All arrays are heap-allocated at init with the requested capacity.
 * The freelist is a stack of free slot indices for O(1) alloc/free.
 */
typedef struct mesh_registry {
    /** Per-slot mesh type (NONE = free). */
    mesh_type_t *types;

    /** Per-slot mesh union (static or skeletal). */
    union mesh_slot {
        static_mesh_t   stat;
        skeletal_mesh_t skel;
    } *meshes;

    /** Per-slot generation counter. */
    uint16_t *generations;

    /** Freelist stack (indices of available slots). */
    uint32_t *freelist;

    /** Number of entries currently on the freelist. */
    uint32_t freelist_count;

    /** Number of live meshes in the registry. */
    uint32_t count;

    /** Total number of slots. */
    uint32_t capacity;

    /** GL function loader passed to mesh creation routines. */
    const gl_loader_t *loader;
} mesh_registry_t;

/* ── Lifecycle ────────────────────────────────────────────────────── */

/**
 * @brief Initialize a mesh registry with the given capacity.
 *
 * Allocates internal arrays via a single malloc.  All slots start on
 * the freelist with generation 0.
 *
 * @param reg       Registry to initialize (non-NULL).
 * @param capacity  Maximum number of meshes (> 0).
 * @param loader    GL function loader (non-NULL).
 * @return Status code.
 */
int mesh_registry_init(mesh_registry_t *reg, uint32_t capacity,
                       const gl_loader_t *loader);

/**
 * @brief Destroy the registry, freeing all meshes and internal arrays.
 *
 * Safe to call with NULL.
 *
 * @param reg  Registry to destroy (NULL-safe).
 */
void mesh_registry_destroy(mesh_registry_t *reg);

/* ── Insertion ────────────────────────────────────────────────────── */

/**
 * @brief Insert a static mesh built from raw data.
 *
 * Creates the mesh via static_mesh_create(), stores it, and returns
 * a handle.  Fails if the registry is full.
 *
 * @param reg     Registry (non-NULL).
 * @param info    Static mesh creation info (non-NULL).
 * @param out     Output handle (non-NULL).
 * @return Status code.
 */
int mesh_registry_insert_static(mesh_registry_t *reg,
                                const static_mesh_create_info_t *info,
                                mesh_handle_t *out);

/**
 * @brief Insert a skeletal mesh built from raw data.
 *
 * Creates the mesh via skeletal_mesh_create(), stores it, and returns
 * a handle.  Fails if the registry is full.
 *
 * @param reg     Registry (non-NULL).
 * @param info    Skeletal mesh creation info (non-NULL).
 * @param out     Output handle (non-NULL).
 * @return Status code.
 */
int mesh_registry_insert_skeletal(mesh_registry_t *reg,
                                  const skeletal_mesh_create_info_t *info,
                                  mesh_handle_t *out);

/* ── Removal ──────────────────────────────────────────────────────── */

/**
 * @brief Remove a mesh by handle.
 *
 * Destroys the mesh, bumps the slot generation, and returns the slot
 * to the freelist.  No-op if the handle is invalid or stale.
 *
 * @param reg     Registry (non-NULL).
 * @param handle  Handle to remove.
 */
void mesh_registry_remove(mesh_registry_t *reg, mesh_handle_t handle);

/* ── Queries ──────────────────────────────────────────────────────── */

/**
 * @brief Check whether a handle refers to a live mesh.
 *
 * @param reg     Registry (non-NULL).
 * @param handle  Handle to check.
 * @return true if valid, false otherwise.
 */
int mesh_registry_is_valid(const mesh_registry_t *reg,
                           mesh_handle_t handle);

/**
 * @brief Get the mesh type for a handle.
 *
 * Returns MESH_TYPE_NONE if the handle is invalid.
 *
 * @param reg     Registry (non-NULL).
 * @param handle  Handle to query.
 * @return Mesh type.
 */
mesh_type_t mesh_registry_type(const mesh_registry_t *reg,
                               mesh_handle_t handle);

/**
 * @brief Get a pointer to a static mesh by handle.
 *
 * Returns NULL if the handle is invalid or the mesh is not static.
 *
 * @param reg     Registry (non-NULL).
 * @param handle  Handle to look up.
 * @return Pointer to the static mesh, or NULL.
 */
const static_mesh_t *mesh_registry_get_static(const mesh_registry_t *reg,
                                               mesh_handle_t handle);

/**
 * @brief Get a pointer to a skeletal mesh by handle.
 *
 * Returns NULL if the handle is invalid or the mesh is not skeletal.
 *
 * @param reg     Registry (non-NULL).
 * @param handle  Handle to look up.
 * @return Pointer to the skeletal mesh, or NULL.
 */
const skeletal_mesh_t *mesh_registry_get_skeletal(const mesh_registry_t *reg,
                                                   mesh_handle_t handle);

/**
 * @brief Return the number of live meshes.
 * @param reg  Registry (non-NULL).
 * @return Live mesh count.
 */
uint32_t mesh_registry_count(const mesh_registry_t *reg);

/**
 * @brief Return the total slot capacity.
 * @param reg  Registry (non-NULL).
 * @return Capacity.
 */
uint32_t mesh_registry_capacity(const mesh_registry_t *reg);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_MESH_REGISTRY_H */
