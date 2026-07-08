/**
 * @file srd_voxel_rule.h
 * @brief Selection model for voxel SDF rewrite rules.
 *
 * A selection identifies which part of which room a rule operates on:
 * a face, a corner, or a sub-region, plus a scalar parameter.
 *
 * Types (2): srd_face_t, srd_voxel_selection_t
 */
#ifndef SRD_VOXEL_RULE_H
#define SRD_VOXEL_RULE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Room face identifiers for selection.
 *
 * Directions follow the engine convention:
 *   +X = East, -X = West, +Y = Up, -Y = Down, +Z = South, -Z = North.
 */
typedef enum {
    SRD_FACE_NORTH =  0, /**< -Z wall */
    SRD_FACE_SOUTH =  1, /**< +Z wall */
    SRD_FACE_EAST  =  2, /**< +X wall */
    SRD_FACE_WEST  =  3, /**< -X wall */
    SRD_FACE_CEIL  =  4, /**< +Y (ceiling) */
    SRD_FACE_FLOOR =  5, /**< -Y (floor) */
    SRD_FACE_NONE  = -1  /**< No face selected (whole room) */
} srd_face_t;

/**
 * @brief Selection for a voxel rewrite rule.
 *
 * Identifies the room, face/corner, and a scalar parameter that
 * controls the magnitude of the rule's effect.
 */
typedef struct {
    uint8_t    room_id; /**< 1-based room ID. */
    srd_face_t face;    /**< Face to operate on, or SRD_FACE_NONE. */
    int        corner;  /**< Corner index 0-3, or -1 for none. */
    float      param;   /**< Rule-specific parameter (voxel count, radius, etc.). */
} srd_voxel_selection_t;

#ifdef __cplusplus
}
#endif

#endif /* SRD_VOXEL_RULE_H */
