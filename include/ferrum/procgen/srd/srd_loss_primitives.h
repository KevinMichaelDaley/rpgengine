#ifndef FERRUM_PROCGEN_SRD_LOSS_PRIMITIVES_H
#define FERRUM_PROCGEN_SRD_LOSS_PRIMITIVES_H

#include <stdint.h>
#include "ferrum/procgen/procgen_srd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute PathDistance loss via eikonal solver.
 */
double srd_loss_path_distance(const fr_room_box_t *rooms, uint32_t n_rooms,
                               uint32_t src_idx, uint32_t tgt_idx,
                               int grid_nx, int grid_nz);

/**
 * @brief Compute LineOfSight loss via transport solver.
 */
double srd_loss_line_of_sight(const fr_room_box_t *rooms, uint32_t n_rooms,
                               uint32_t src_idx, uint32_t tgt_idx,
                               int grid_nx, int grid_nz);

/**
 * @brief Compute NonPenetration (Gaussian overlap) loss.
 */
double srd_loss_non_penetration(const fr_room_box_t *rooms, uint32_t n_rooms);

/**
 * @brief Compute MinimumSize loss.
 */
double srd_loss_minimum_size(const fr_room_box_t *room, double min_half);

/**
 * @brief Compute Separation loss between two rooms.
 */
double srd_loss_separation(const fr_room_box_t *a, const fr_room_box_t *b,
                            double target_dist, int op); /* 0=>, 1=< */

/**
 * @brief Compute HeightSpan loss.
 */
double srd_loss_height_span(const fr_room_box_t *room,
                             double min_height, double max_height);

/**
 * @brief Compute StairAlignment loss.
 */
double srd_loss_stair_alignment(double ax, double az, double tx, double tz);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_LOSS_PRIMITIVES_H */
