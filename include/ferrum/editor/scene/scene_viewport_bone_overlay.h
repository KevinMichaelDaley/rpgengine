/**
 * @file scene_viewport_bone_overlay.h
 * @brief Bone capsule overlay rendering for skeleton visualization.
 *
 * Computes capsule geometry (center, axis, length, radius) from joint
 * head/tail positions, and renders bones as wireframe capsules in the
 * viewport when a skeleton entity is selected.
 *
 * Public types: bone_capsule_params_t (1 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_VIEWPORT_BONE_OVERLAY_H
#define FERRUM_EDITOR_SCENE_VIEWPORT_BONE_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Forward declarations. */
struct viewport_render_state;
struct skeleton_def;
struct edit_bone_selection;

/** @brief Minimum capsule radius for very short or zero-length bones. */
#define BONE_CAPSULE_MIN_RADIUS 0.01f

/** @brief Maximum capsule radius cap for very long bones. */
#define BONE_CAPSULE_MAX_RADIUS 0.15f

/** @brief Radius-to-length proportion factor. */
#define BONE_CAPSULE_RADIUS_FACTOR 0.08f

/**
 * @brief Capsule geometry parameters computed from a bone's head/tail.
 */
typedef struct bone_capsule_params {
    float center[3]; /**< Midpoint between head and tail. */
    float axis[3];   /**< Unit direction from head to tail (zero if degenerate). */
    float length;    /**< Distance from head to tail. */
    float radius;    /**< Capsule radius, proportional to length, clamped. */
} bone_capsule_params_t;

/**
 * @brief Compute capsule parameters from joint head and tail positions.
 *
 * Center is the midpoint of head and tail. Length is the Euclidean
 * distance. Radius is length * BONE_CAPSULE_RADIUS_FACTOR, clamped
 * to [BONE_CAPSULE_MIN_RADIUS, BONE_CAPSULE_MAX_RADIUS]. For
 * zero-length bones, axis is set to (0,1,0) and radius to min.
 *
 * @param head       Head position (3 floats, non-NULL).
 * @param tail       Tail position (3 floats, non-NULL).
 * @param out_params Output capsule parameters (non-NULL).
 */
void bone_capsule_params_from_joint(const float *head, const float *tail,
                                     bone_capsule_params_t *out_params);

/**
 * @brief Draw bone overlay for a skeleton entity.
 *
 * Renders all bones as wireframe capsules. Selected bones are
 * highlighted in yellow; the active bone in white; unselected in
 * light blue.
 *
 * @param state       Viewport render state (non-NULL).
 * @param skel        Skeleton definition (non-NULL).
 * @param entity_id   Entity owning the skeleton.
 * @param model       4x4 model matrix (column-major, 16 floats, non-NULL).
 * @param bone_sel    Bone selection state (may be NULL for no selection).
 */
void viewport_render_draw_bone_overlay(
    struct viewport_render_state *state,
    const struct skeleton_def *skel,
    uint32_t entity_id,
    const float *model,
    const struct edit_bone_selection *bone_sel);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_VIEWPORT_BONE_OVERLAY_H */
