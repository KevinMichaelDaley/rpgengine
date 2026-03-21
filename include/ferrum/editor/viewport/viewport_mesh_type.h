/**
 * @file viewport_mesh_type.h
 * @brief Per-entity mesh type tracking for static/skeletal switching.
 *
 * Tracks whether each entity's mesh is static or skeletal. Supports
 * one-way upgrade from static to skeletal (skeleton assignment), but
 * FORBIDS downgrade from skeletal to static (lossy, destructive).
 *
 * Public types: viewport_mesh_type_t (1 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_MESH_TYPE_H
#define FERRUM_EDITOR_VIEWPORT_MESH_TYPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Per-entity mesh type classification.
 *
 * Determines how the entity's mesh is rendered and which GPU
 * resources are allocated (static_mesh_t vs skeletal_mesh_t).
 */
typedef enum viewport_mesh_type {
    VIEWPORT_MESH_NONE     = 0, /**< No mesh loaded. */
    VIEWPORT_MESH_STATIC   = 1, /**< Static mesh (no bone data). */
    VIEWPORT_MESH_SKELETAL = 2, /**< Skeletal mesh (bone weights + indices). */
} viewport_mesh_type_t;

/**
 * @brief Check whether a mesh type transition is allowed.
 *
 * Allowed transitions:
 *   NONE     → STATIC    (loading a mesh)
 *   NONE     → SKELETAL  (loading a skeletal mesh)
 *   STATIC   → SKELETAL  (assigning a skeleton)
 *   STATIC   → NONE      (unloading mesh)
 *   same     → same      (no-op)
 *
 * Forbidden transitions:
 *   SKELETAL → STATIC    (lossy — would discard bone data)
 *   SKELETAL → NONE      (lossy — would discard bone data)
 *
 * @param from  Current mesh type.
 * @param to    Desired mesh type.
 * @return true if the transition is allowed.
 */
bool viewport_mesh_type_can_upgrade(viewport_mesh_type_t from,
                                     viewport_mesh_type_t to);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_MESH_TYPE_H */
