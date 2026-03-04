#ifndef FERRUM_RENDERER_MESH_HANDLE_H
#define FERRUM_RENDERER_MESH_HANDLE_H

/**
 * @file mesh_handle.h
 * @brief Opaque handle for referencing meshes in a mesh_registry_t.
 *
 * The handle stores an index into the registry's slot array and a
 * generation counter that is incremented on removal.  Stale handles
 * (pointing at a recycled slot) are detected by comparing generations.
 *
 * @note Thread safety: handles are plain value types with no
 *       synchronization.  The registry itself is not thread-safe.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Handle ───────────────────────────────────────────────────────── */

/**
 * @brief Opaque mesh handle (index + generation).
 *
 * index  — slot within the registry's internal arrays.
 * generation — incremented each time a slot is freed; prevents
 *              use-after-free via stale handles.
 */
typedef struct mesh_handle {
    uint32_t index;
    uint16_t generation;
} mesh_handle_t;

/* ── Mesh type ────────────────────────────────────────────────────── */

/**
 * @brief Discriminator for the mesh stored in a registry slot.
 */
typedef enum mesh_type {
    MESH_TYPE_NONE     = 0, /**< Slot is empty / on freelist. */
    MESH_TYPE_STATIC   = 1, /**< static_mesh_t. */
    MESH_TYPE_SKELETAL = 2  /**< skeletal_mesh_t. */
} mesh_type_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_MESH_HANDLE_H */
