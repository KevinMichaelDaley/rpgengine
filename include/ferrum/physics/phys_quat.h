#ifndef FERRUM_PHYSICS_PHYS_QUAT_H
#define FERRUM_PHYSICS_PHYS_QUAT_H

#include <stdint.h>

#include "ferrum/math/quat.h"

/** @file
 * @brief Physics-facing quaternion type.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Physics quaternion (bit-compatible with quat_t). */
typedef quat_t phys_quat_t;

/** Convert engine quat_t -> phys_quat_t. */
#define PHYS_QUAT_FROM_QUAT(q) ((phys_quat_t){(q).x, (q).y, (q).z, (q).w})

/** Convert phys_quat_t -> engine quat_t. */
#define QUAT_FROM_PHYS_QUAT(q) ((quat_t){(q).x, (q).y, (q).z, (q).w})

_Static_assert(sizeof(phys_quat_t) == 16, "phys_quat_t must be 16 bytes");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PHYS_QUAT_H */
