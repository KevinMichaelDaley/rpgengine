/**
 * @file constraint_space.h
 * @brief Space conversion API for constraint evaluation.
 *
 * Converts transforms between world/local/pose/bone coordinate
 * frames. Used by the constraint solver for both animation bones
 * and physics bodies.
 *
 * Public types: 0 (uses types from constraint_types.h and constraint_params.h)
 * Public functions: 3
 */

#ifndef FERRUM_ANIMATION_CONSTRAINT_SPACE_H
#define FERRUM_ANIMATION_CONSTRAINT_SPACE_H

#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert a transform from the given space to world space.
 *
 * @param skel      Skeleton definition (for parent/rest data).
 * @param bone_idx  Index of the bone this transform belongs to.
 * @param space     Source coordinate space.
 * @param input     Input transform in source space (non-NULL).
 * @param out_world Output transform in world space (non-NULL).
 *
 * Space semantics:
 * - WORLD: out = input (no conversion)
 * - LOCAL: out = parent_world × input
 * - POSE:  out = rest_world × input
 * - BONE:  out = rest_world × rest_local_inv × rest_local × input
 *          (= rest_world × input, since rest_local is the bone's own basis)
 */
void constraint_to_world_space(const skeleton_def_t *skel, uint32_t bone_idx,
                                constraint_space_t space,
                                const mat4_t *input, mat4_t *out_world);

/**
 * @brief Convert a transform from world space to the given space.
 *
 * Inverse of constraint_to_world_space().
 *
 * @param skel      Skeleton definition.
 * @param bone_idx  Bone index.
 * @param space     Target coordinate space.
 * @param world     Input transform in world space (non-NULL).
 * @param out_local Output transform in target space (non-NULL).
 */
void constraint_from_world_space(const skeleton_def_t *skel, uint32_t bone_idx,
                                  constraint_space_t space,
                                  const mat4_t *world, mat4_t *out_local);

/**
 * @brief Blend between original and constrained transforms.
 *
 * Performs per-element linear interpolation on the translation
 * components, and element-wise lerp on the full matrix for simplicity.
 * For rotation-heavy blending, callers should decompose to TRS.
 *
 * @param original    Unconstrained transform (non-NULL).
 * @param constrained Fully constrained transform (non-NULL).
 * @param influence   Blend factor 0.0 (original) to 1.0 (constrained).
 * @param out         Output blended transform (non-NULL).
 */
void constraint_blend_influence(const mat4_t *original,
                                const mat4_t *constrained,
                                float influence, mat4_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_CONSTRAINT_SPACE_H */
