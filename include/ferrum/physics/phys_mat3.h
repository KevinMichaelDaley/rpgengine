#ifndef FERRUM_PHYSICS_PHYS_MAT3_H
#define FERRUM_PHYSICS_PHYS_MAT3_H

#include <stdint.h>

/** @file
 * @brief Physics-facing 3x3 matrix type (column-major), used for inertia tensors.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Physics 3x3 matrix stored as 9 floats (column-major). */
typedef struct phys_mat3 {
    float m[9];
} phys_mat3_t;

_Static_assert(sizeof(phys_mat3_t) == 36, "phys_mat3_t must be 36 bytes");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PHYS_MAT3_H */
