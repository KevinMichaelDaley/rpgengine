#ifndef FERRUM_PHYSICS_PHYS_MAT3_H
#define FERRUM_PHYSICS_PHYS_MAT3_H

#include <stdint.h>

#include "ferrum/physics/phys_vec3.h"
#include "ferrum/physics/phys_quat.h"

/** @file
 * @brief Physics-facing 3x3 matrix type (column-major), used for inertia tensors.
 *
 * Column-major layout: m[col*3 + row].
 *   Column 0: m[0], m[1], m[2]
 *   Column 1: m[3], m[4], m[5]
 *   Column 2: m[6], m[7], m[8]
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Physics 3x3 matrix stored as 9 floats (column-major). */
typedef struct phys_mat3 {
    float m[9];
} phys_mat3_t;

_Static_assert(sizeof(phys_mat3_t) == 36, "phys_mat3_t must be 36 bytes");

/**
 * @brief Multiply a 3x3 matrix by a vec3: result = M * v.
 *
 * @param M  Column-major 3x3 matrix.
 * @param v  Input vector.
 * @return   M * v.
 */
phys_vec3_t phys_mat3_mul_vec3(const phys_mat3_t *M, phys_vec3_t v);

/**
 * @brief Compute the world-space inverse inertia tensor from a body-local
 *        diagonal inverse inertia and a rotation quaternion.
 *
 * Result = R * diag(inv_I_diag) * R^T, where R is the rotation matrix
 * corresponding to the quaternion q.  This is a symmetric 3x3 matrix.
 *
 * @param q            Body orientation quaternion (unit).
 * @param inv_I_diag   Diagonal inverse inertia in body-local frame.
 * @return             World-space 3x3 inverse inertia tensor.
 */
phys_mat3_t phys_mat3_inv_inertia_world(phys_quat_t q, phys_vec3_t inv_I_diag);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PHYS_MAT3_H */
