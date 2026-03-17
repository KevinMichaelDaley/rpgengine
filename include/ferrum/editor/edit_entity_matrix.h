/**
 * @file edit_entity_matrix.h
 * @brief Compute model matrix for editor entities.
 *
 * Builds the T(pos) * R(quat) * S(scale) * T(-pivot) model matrix
 * used for rendering, raycasting, and snap target computation.
 *
 * Ownership: no allocations; pure math.
 * Nullability: entity must be non-NULL.
 * Error semantics: returns identity if entity is NULL.
 * Side effects: none.
 *
 * Public types: none (uses edit_entity_t from edit_entity.h).
 */
#ifndef FERRUM_EDITOR_EDIT_ENTITY_MATRIX_H
#define FERRUM_EDITOR_EDIT_ENTITY_MATRIX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/math/mat4.h"

/* Forward declaration. */
struct edit_entity;

/**
 * @brief Compute the model matrix for an entity.
 *
 * Model = T(pos) * R(orientation) * S(scale) * T(-pivot_offset).
 *
 * @param ent  Entity (non-NULL).
 * @return 4x4 model matrix (identity if ent is NULL).
 */
mat4_t edit_entity_build_model_matrix(const struct edit_entity *ent);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_ENTITY_MATRIX_H */
