#ifndef FERRUM_PROCGEN_SRD_ENERGY_H
#define FERRUM_PROCGEN_SRD_ENERGY_H

#include <symx>

namespace srd {

/**
 * @brief Energy for a room box at a target position.
 *
 * Simple quadratic: (cx - target_cx)^2 + (cz - target_cz)^2.
 * Zero when room is at target, positive otherwise.
 */
symx::Scalar srd_room_dist_energy(symx::Workspace &ws,
                                  symx::Scalar cx, symx::Scalar cz,
                                  double target_cx, double target_cz);

/**
 * @brief SDF-based room energy (single center sample).
 *
 * Evaluates the box SDF at the room center and returns the squared
 * occupancy error against target occupancy = 1.
 */
symx::Scalar srd_room_sdf_energy(symx::Workspace &ws,
                                 symx::Scalar cx, symx::Scalar cz,
                                 symx::Scalar hx, symx::Scalar hy, symx::Scalar hz,
                                 double temperature);

/**
 * @brief SDF-based corridor energy (centerline sample).
 *
 * Evaluates capped-cylinder SDF at the corridor midpoint and returns
 * the squared occupancy error.
 */
symx::Scalar srd_corridor_sdf_energy(symx::Workspace &ws,
                                     symx::Scalar fx, symx::Scalar fz,
                                     symx::Scalar tx, symx::Scalar tz,
                                     symx::Scalar radius,
                                     symx::Scalar floor_y, symx::Scalar ceil_y,
                                     double temperature);

/**
 * @brief Stair alignment energy.
 *
 * Simple quadratic: (anchor_x - target_x)^2 + (anchor_z - target_z)^2.
 * Zero when stair anchors are aligned, positive otherwise.
 */
symx::Scalar srd_stair_alignment_energy(symx::Workspace &ws,
                                        symx::Scalar anchor_x, symx::Scalar anchor_z,
                                        symx::Scalar target_x, symx::Scalar target_z);

/**
 * @brief Overlap penalty between two rooms.
 *
 * Evaluates the product of occupancy of both rooms at the midpoint
 * between their centers. Returns 0 if rooms are far apart, >0 if overlapping.
 */
symx::Scalar srd_overlap_energy(symx::Workspace &ws,
                                symx::Scalar ax, symx::Scalar az,
                                symx::Scalar ahx, symx::Scalar ahy, symx::Scalar ahz,
                                symx::Scalar bx, symx::Scalar bz,
                                symx::Scalar bhx, symx::Scalar bhy, symx::Scalar bhz,
                                double temperature);

/**
 * @brief Differentiable proxy for PathDistance via Euclidean distance.
 *
 * loss = max(0, |p_a - p_b| - target)²
 * Gradients computed by SymX symbolic differentiation.
 */
symx::Scalar srd_path_distance_energy(symx::Workspace &ws,
                                       symx::Scalar ax, symx::Scalar az,
                                       symx::Scalar bx, symx::Scalar bz,
                                       double target_distance);

/**
 * @brief Differentiable proxy for LineOfSight via dot-product falloff.
 *
 * loss = (1 - clamp(dot(d̂, b-a) / |b-a|, 0, 1))²
 */
symx::Scalar srd_line_of_sight_energy(symx::Workspace &ws,
                                       symx::Scalar ax, symx::Scalar az,
                                       symx::Scalar bx, symx::Scalar bz);

} /* namespace srd */

#endif /* FERRUM_PROCGEN_SRD_ENERGY_H */
