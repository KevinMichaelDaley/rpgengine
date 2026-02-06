#ifndef FERRUM_PHYSICS_PHYS_VEC3_H
#define FERRUM_PHYSICS_PHYS_VEC3_H

#include <stdint.h>

#include "ferrum/math/vec3.h"

/** @file
 * @brief Physics-facing 3D vector type.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Physics 3D vector (bit-compatible with vec3_t). */
typedef vec3_t phys_vec3_t;

/** Convert engine vec3_t -> phys_vec3_t. */
#define PHYS_VEC3_FROM_VEC3(v) ((phys_vec3_t){(v).x, (v).y, (v).z})

/** Convert phys_vec3_t -> engine vec3_t. */
#define VEC3_FROM_PHYS_VEC3(v) ((vec3_t){(v).x, (v).y, (v).z})

_Static_assert(sizeof(phys_vec3_t) == 12, "phys_vec3_t must be 12 bytes");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PHYS_VEC3_H */
