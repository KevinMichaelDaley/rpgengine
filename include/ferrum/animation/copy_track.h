/**
 * @file copy_track.h
 * @brief Copy and tracking constraint evaluators.
 *
 * Provides evaluators for Copy Transforms, Copy Rotation, Copy Location,
 * Copy Scale, Damped Track, Track To, and Locked Track constraints.
 *
 * Public types: 0 (uses types from constraint_solver.h)
 * Public functions: 1 (copy_track_register)
 */

#ifndef FERRUM_ANIMATION_COPY_TRACK_H
#define FERRUM_ANIMATION_COPY_TRACK_H

#include "ferrum/animation/constraint_solver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register all copy and tracking constraint evaluators.
 *
 * Registers evaluators for:
 *   CONSTRAINT_COPY_TRANSFORMS, CONSTRAINT_COPY_ROTATION,
 *   CONSTRAINT_COPY_LOCATION, CONSTRAINT_COPY_SCALE,
 *   CONSTRAINT_DAMPED_TRACK, CONSTRAINT_TRACK_TO, CONSTRAINT_LOCKED_TRACK
 *
 * @param solver Solver to register with (non-NULL).
 */
void copy_track_register(constraint_solver_t *solver);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_COPY_TRACK_H */
