/**
 * @file srd_voxel_rule_table.h
 * @brief Rule dispatch table for voxel SDF rewrite rules.
 *
 * Each entry describes a single rule: its function pointer, the face/corner
 * constraints for random selection, parameter range, and a human-readable
 * name for verbose logging.
 *
 * Types (2): srd_voxel_rule_fn, srd_voxel_rule_entry_t
 */
#ifndef SRD_VOXEL_RULE_TABLE_H
#define SRD_VOXEL_RULE_TABLE_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_voxel_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Signature for all voxel rewrite rule functions.
 *
 * @param grid SDF grid to modify.
 * @param map  Room map to modify.
 * @param sel  Selection (room, face, corner, param).
 * @return 0 on success, -1 on invalid input.
 */
typedef int (*srd_voxel_rule_fn)(srd_sdf_grid_t *grid, srd_room_map_t *map,
                                 const srd_voxel_selection_t *sel);

/**
 * @brief Descriptor for a single voxel rewrite rule.
 *
 * Used by the descent loop to randomly sample valid (rule, selection) pairs.
 */
typedef struct {
    srd_voxel_rule_fn apply;     /**< The rule function. */
    srd_face_t required_face;    /**< Required face, or SRD_FACE_NONE for any. */
    int        needs_corner;     /**< 1 if corner must be 0-3, 0 otherwise. */
    float      param_min;        /**< Minimum param value for random sampling. */
    float      param_max;        /**< Maximum param value for random sampling. */
    const char *name;            /**< Human-readable name for logging. */
} srd_voxel_rule_entry_t;

/**
 * @brief Get the default table of all 18 voxel rewrite rules.
 *
 * Returns a pointer to a static array of rule entries. The array is
 * valid for the lifetime of the program.
 *
 * @param[out] n_rules Number of rules in the table.
 * @return Pointer to static rule array. Never NULL.
 */
const srd_voxel_rule_entry_t *srd_voxel_rule_table_default(int *n_rules);

#ifdef __cplusplus
}
#endif

#endif /* SRD_VOXEL_RULE_TABLE_H */
