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

} /* namespace srd */

#endif /* FERRUM_PROCGEN_SRD_ENERGY_H */
