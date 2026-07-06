/**
 * @file srd_room_type.h
 * @brief Room type enum and box flag constants for the SRD SDF layout.
 *
 * Split from srd_sdf_layout.h to satisfy the 2-type-per-header rule.
 */
#ifndef FERRUM_PROCGEN_SRD_ROOM_TYPE_H
#define FERRUM_PROCGEN_SRD_ROOM_TYPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Minimum half-extent for newly spawned boxes.
 *
 * All Add* rules create boxes at this size to satisfy jump continuity:
 * the rendered change is negligible, and gradient-based optimisation
 * grows the box to the correct size.
 */
#define SRD_EPSILON 0.01f

/** @brief Maximum number of boxes in a single SDF layout. */
#define SRD_MAX_BOXES 512

/** @brief Maximum number of door slots per box (N, S, E, W). */
#define SRD_MAX_DOORS 4

/**
 * @brief Semantic room types for the SDF layout.
 *
 * Each box carries a type that the critic uses to apply type-specific
 * loss terms (e.g., boss rooms penalised for small size, treasure rooms
 * penalised if not dead-ends).
 */
typedef enum {
    SRD_ROOM_GENERIC   = 0,  /**< General-purpose room */
    SRD_ROOM_BAR       = 1,  /**< Bar / tavern area */
    SRD_ROOM_ENTRANCE  = 2,  /**< Dungeon entrance / gateway */
    SRD_ROOM_PRIVATE   = 3,  /**< Private / restricted area */
    SRD_ROOM_STAIR_UP  = 4,  /**< Staircase going up */
    SRD_ROOM_STAIR_DOWN = 5, /**< Staircase going down */
    SRD_ROOM_CORRIDOR  = 6,  /**< Corridor connecting rooms */
    SRD_ROOM_DEAD_END  = 7,  /**< Dead-end branch */
    SRD_ROOM_SECRET    = 8,  /**< Hidden / secret room */
    SRD_ROOM_BOSS      = 9,  /**< Boss encounter room */
    SRD_ROOM_TREASURE  = 10, /**< Treasure / reward room */
    SRD_ROOM_TYPE_COUNT = 11
} srd_room_type_t;

/**
 * @brief Box flag: this box was spawned at SRD_EPSILON size.
 *
 * Set by Add* rules at creation. Cleared by the continuous optimiser
 * once the box has been grown past 2 * SRD_EPSILON.
 */
#define SRD_BOX_EPSILON     ((uint32_t)(1u << 0))

/**
 * @brief Box flag: this box was created by a repair rule.
 *
 * Informational only; does not affect optimisation.
 */
#define SRD_BOX_REPAIR_ONLY ((uint32_t)(1u << 1))

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_ROOM_TYPE_H */
