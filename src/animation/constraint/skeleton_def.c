/**
 * @file skeleton_def.c
 * @brief Skeleton definition init and destroy.
 */

#include "ferrum/animation/constraint_params.h"
#include <stdlib.h>
#include <string.h>

bool skeleton_def_init(skeleton_def_t *skel, uint32_t joint_count,
                       uint32_t max_constraints) {
    if (!skel || joint_count == 0) {
        return false;
    }

    memset(skel, 0, sizeof(*skel));
    skel->joint_count = joint_count;
    skel->max_constraints_per_joint = max_constraints;

    /* Allocate all arrays. */
    skel->joint_names = calloc(joint_count, sizeof(*skel->joint_names));
    skel->parent_indices = calloc(joint_count, sizeof(*skel->parent_indices));
    skel->rest_local = calloc(joint_count, sizeof(*skel->rest_local));
    skel->rest_world = calloc(joint_count, sizeof(*skel->rest_world));
    skel->constraint_counts = calloc(joint_count, sizeof(*skel->constraint_counts));

    if (!skel->joint_names || !skel->parent_indices ||
        !skel->rest_local || !skel->rest_world || !skel->constraint_counts) {
        skeleton_def_destroy(skel);
        return false;
    }

    /* Constraint array (may be zero if max_constraints == 0). */
    if (max_constraints > 0) {
        skel->constraints = calloc((size_t)joint_count * max_constraints,
                                   sizeof(*skel->constraints));
        if (!skel->constraints) {
            skeleton_def_destroy(skel);
            return false;
        }
    }

    /* Initialize parent indices to UINT32_MAX (root marker). */
    for (uint32_t i = 0; i < joint_count; i++) {
        skel->parent_indices[i] = UINT32_MAX;
    }

    /* Initialize rest transforms to identity. */
    mat4_t ident = mat4_identity();
    for (uint32_t i = 0; i < joint_count; i++) {
        skel->rest_local[i] = ident;
        skel->rest_world[i] = ident;
    }

    return true;
}

void skeleton_def_destroy(skeleton_def_t *skel) {
    if (!skel) return;

    free(skel->joint_names);
    free(skel->parent_indices);
    free(skel->rest_local);
    free(skel->rest_world);
    free(skel->constraint_counts);
    free(skel->constraints);

    memset(skel, 0, sizeof(*skel));
}
