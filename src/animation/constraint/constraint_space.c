/**
 * @file constraint_space.c
 * @brief Space conversion between world/local/pose/bone frames.
 *
 * Non-static functions: 3 (to_world, from_world, blend_influence)
 */

#include "ferrum/animation/constraint_space.h"
#include "ferrum/math/mat4.h"

void constraint_to_world_space(const skeleton_def_t *skel, uint32_t bone_idx,
                                constraint_space_t space,
                                const mat4_t *input, mat4_t *out_world) {
    switch (space) {
    case CONSTRAINT_SPACE_WORLD:
        /* Already in world space. */
        *out_world = *input;
        break;

    case CONSTRAINT_SPACE_LOCAL: {
        /* Local = relative to parent. world = parent_world × input. */
        uint32_t parent = skel->parent_indices[bone_idx];
        if (parent == UINT32_MAX) {
            *out_world = *input;
        } else {
            *out_world = mat4_mul(skel->rest_world[parent], *input);
        }
        break;
    }

    case CONSTRAINT_SPACE_POSE:
        /* Pose = relative to rest_world. world = rest_world × input. */
        *out_world = mat4_mul(skel->rest_world[bone_idx], *input);
        break;

    case CONSTRAINT_SPACE_BONE:
        /* Bone = relative to bone's own rest local transform.
         * world = parent_world × rest_local × input. */
        {
            uint32_t parent = skel->parent_indices[bone_idx];
            mat4_t bone_in_parent = mat4_mul(skel->rest_local[bone_idx], *input);
            if (parent == UINT32_MAX) {
                *out_world = bone_in_parent;
            } else {
                *out_world = mat4_mul(skel->rest_world[parent], bone_in_parent);
            }
        }
        break;
    }
}

void constraint_from_world_space(const skeleton_def_t *skel, uint32_t bone_idx,
                                  constraint_space_t space,
                                  const mat4_t *world, mat4_t *out_local) {
    switch (space) {
    case CONSTRAINT_SPACE_WORLD:
        *out_local = *world;
        break;

    case CONSTRAINT_SPACE_LOCAL: {
        uint32_t parent = skel->parent_indices[bone_idx];
        if (parent == UINT32_MAX) {
            *out_local = *world;
        } else {
            mat4_t parent_inv;
            mat4_inverse(skel->rest_world[parent], &parent_inv);
            *out_local = mat4_mul(parent_inv, *world);
        }
        break;
    }

    case CONSTRAINT_SPACE_POSE: {
        mat4_t rest_inv;
        mat4_inverse(skel->rest_world[bone_idx], &rest_inv);
        *out_local = mat4_mul(rest_inv, *world);
        break;
    }

    case CONSTRAINT_SPACE_BONE: {
        /* Reverse of: world = parent_world × rest_local × input.
         * input = rest_local_inv × parent_world_inv × world. */
        uint32_t parent = skel->parent_indices[bone_idx];
        mat4_t in_parent;
        if (parent == UINT32_MAX) {
            in_parent = *world;
        } else {
            mat4_t parent_inv;
            mat4_inverse(skel->rest_world[parent], &parent_inv);
            in_parent = mat4_mul(parent_inv, *world);
        }
        mat4_t rest_local_inv;
        mat4_inverse(skel->rest_local[bone_idx], &rest_local_inv);
        *out_local = mat4_mul(rest_local_inv, in_parent);
        break;
    }
    }
}

void constraint_blend_influence(const mat4_t *original,
                                const mat4_t *constrained,
                                float influence, mat4_t *out) {
    /* Element-wise lerp: out = original + influence * (constrained - original). */
    for (int i = 0; i < 16; i++) {
        out->m[i] = original->m[i] +
                    influence * (constrained->m[i] - original->m[i]);
    }
}
