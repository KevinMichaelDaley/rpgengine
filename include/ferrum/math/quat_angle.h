#ifndef FERRUM_MATH_QUAT_ANGLE_H
#define FERRUM_MATH_QUAT_ANGLE_H

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

/** @file
 * @brief Quaternion angle + angular velocity integration helpers.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Angular difference between two orientations in degrees.
 *
 * Uses the shortest-arc distance and treats q and -q as equivalent.
 */
float fr_quat_angle_degrees_between(quat_t a, quat_t b);

/**
 * @brief Integrate quaternion forward by constant angular velocity for dt.
 *
 * @param q Starting orientation.
 * @param omega_rad_s Angular velocity vector in radians/sec (axis * speed).
 * @param dt_s Delta time in seconds.
 * @param epsilon Small epsilon for normalization guards.
 */
quat_t fr_quat_integrate_angular_velocity(quat_t q, vec3_t omega_rad_s, float dt_s, float epsilon);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_MATH_QUAT_ANGLE_H */
