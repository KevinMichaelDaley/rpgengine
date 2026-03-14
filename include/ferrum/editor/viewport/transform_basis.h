/**
 * @file transform_basis.h
 * @brief Transform basis (coordinate space) for gizmo orientation.
 *
 * Defines the coordinate space used for gizmo rendering and transform
 * application: world (global axes), local (active object's rotation),
 * view (camera-aligned axes), or cursor (world axes at cursor position).
 *
 * Ownership: no dynamic allocation; basis matrices are value types.
 * Nullability: pointer params must be non-NULL.
 * Error semantics: returns identity on invalid input.
 * Side effects: none (pure math).
 *
 * Public types: transform_basis_t (enum).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_TRANSFORM_BASIS_H
#define FERRUM_EDITOR_VIEWPORT_TRANSFORM_BASIS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"

/**
 * @brief Transform coordinate space for gizmo and transforms.
 */
typedef enum transform_basis {
    TRANSFORM_BASIS_WORLD  = 0, /**< Global XYZ axes. */
    TRANSFORM_BASIS_LOCAL  = 1, /**< Active object's local rotation. */
    TRANSFORM_BASIS_VIEW   = 2, /**< Camera-aligned axes. */
    TRANSFORM_BASIS_CURSOR = 3, /**< World axes at 3D cursor position. */
} transform_basis_t;

/**
 * @brief Get a human-readable name for a basis mode.
 * @param basis  Basis mode.
 * @return Static string (e.g., "World", "Local").
 */
const char *transform_basis_name(transform_basis_t basis);

/**
 * @brief Cycle to the next basis mode.
 * @param basis  Current basis mode.
 * @return Next mode (wraps from CURSOR back to WORLD).
 */
transform_basis_t transform_basis_next(transform_basis_t basis);

/**
 * @brief Compute the orientation matrix for a given basis mode.
 *
 * Returns a rotation-only mat4 (upper-left 3x3 is the orientation,
 * translation is zero). The gizmo axes are the columns of this matrix.
 *
 * @param basis              Basis mode.
 * @param entity_orientation Active object's quaternion orientation.
 *                           Used only for LOCAL mode. May be NULL if not LOCAL.
 * @param view_matrix        Camera view matrix. Used only for VIEW mode.
 *                           May be NULL if not VIEW.
 * @return Orientation matrix (identity for WORLD/CURSOR).
 */
mat4_t transform_basis_orientation(transform_basis_t basis,
                                    const quat_t *entity_orientation,
                                    const mat4_t *view_matrix);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_TRANSFORM_BASIS_H */
