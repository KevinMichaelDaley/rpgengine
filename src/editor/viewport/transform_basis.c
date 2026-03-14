/**
 * @file transform_basis.c
 * @brief Transform basis orientation computation.
 *
 * Non-static functions (3 / 4 limit):
 *   transform_basis_name
 *   transform_basis_next
 *   transform_basis_orientation
 */

#include "ferrum/editor/viewport/transform_basis.h"
#include "ferrum/math/quat.h"
#include <math.h>

const char *transform_basis_name(transform_basis_t basis) {
    switch (basis) {
    case TRANSFORM_BASIS_WORLD:  return "World";
    case TRANSFORM_BASIS_LOCAL:  return "Local";
    case TRANSFORM_BASIS_VIEW:   return "View";
    case TRANSFORM_BASIS_CURSOR: return "Cursor";
    default:                     return "World";
    }
}

transform_basis_t transform_basis_next(transform_basis_t basis) {
    switch (basis) {
    case TRANSFORM_BASIS_WORLD:  return TRANSFORM_BASIS_LOCAL;
    case TRANSFORM_BASIS_LOCAL:  return TRANSFORM_BASIS_VIEW;
    case TRANSFORM_BASIS_VIEW:   return TRANSFORM_BASIS_CURSOR;
    case TRANSFORM_BASIS_CURSOR: return TRANSFORM_BASIS_WORLD;
    default:                     return TRANSFORM_BASIS_WORLD;
    }
}

mat4_t transform_basis_orientation(transform_basis_t basis,
                                    const quat_t *entity_orientation,
                                    const mat4_t *view_matrix) {
    switch (basis) {
    case TRANSFORM_BASIS_LOCAL: {
        if (!entity_orientation) return mat4_identity();
        /* Convert the entity's quaternion orientation to a rotation matrix. */
        mat4_t rot;
        quat_to_mat4(*entity_orientation, &rot);
        return rot;
    }

    case TRANSFORM_BASIS_VIEW: {
        if (!view_matrix) return mat4_identity();
        /* The view matrix transforms world→view space. The upper-left
         * 3x3 is the rotation part. Its transpose (= inverse for
         * orthonormal) gives view→world, which orients the gizmo
         * to face the camera. */
        mat4_t rot = *view_matrix;
        /* Zero out translation (column 3). */
        rot.m[12] = 0.0f;
        rot.m[13] = 0.0f;
        rot.m[14] = 0.0f;
        rot.m[15] = 1.0f;
        /* Also zero the bottom row except [3][3]. */
        rot.m[3]  = 0.0f;
        rot.m[7]  = 0.0f;
        rot.m[11] = 0.0f;
        return mat4_transpose(rot);
    }

    case TRANSFORM_BASIS_CURSOR:
    case TRANSFORM_BASIS_WORLD:
    default:
        return mat4_identity();
    }
}
