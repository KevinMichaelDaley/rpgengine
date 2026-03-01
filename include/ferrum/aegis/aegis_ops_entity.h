/**
 * @file aegis_ops_entity.h
 * @brief Aegis entity query instruction handlers.
 *
 * Per ref/aegis_bytecode_spec.md §3.3 (Entity query group).
 *
 * All reads go through the script_entity_view_t snapshot. Well-known
 * keys (pos, rot, scale, type, body_index) read directly from snapshot
 * fields; dynamic keys delegate to entity_attrs_get().
 *
 * Ownership: all register pointers are caller-owned; view is read-only.
 * Nullability: view may be NULL (returns false / error).
 */

#ifndef FERRUM_AEGIS_OPS_ENTITY_H
#define FERRUM_AEGIS_OPS_ENTITY_H

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/aegis/aegis_types.h"

/* Forward-declare to avoid pulling in the full script_env header. */
struct script_entity_view;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Find entity in snapshot by ID.
 *
 * Scans the snapshot array for an entity with matching entity_id.
 * Sets dst->i32 to the snapshot index (handle) or -1 if not found.
 *
 * @param dst      Destination register. Must not be NULL.
 * @param id_reg   Register containing the entity_id (u32).
 * @param view     Entity snapshot view. Must not be NULL.
 * @return true on success (even if not found; dst=-1), false if view is NULL.
 */
bool aegis_op_query_entity(aegis_register_t *dst,
                           const aegis_register_t *id_reg,
                           const struct script_entity_view *view);

/**
 * @brief Read attribute from entity snapshot.
 *
 * For well-known keys (POS, ROT, SCALE, TYPE, BODY_IDX), reads directly
 * from the snapshot struct. For dynamic keys, delegates to entity_attrs_get.
 *
 * @param dst    Destination register. Must not be NULL.
 * @param handle Snapshot index returned by query_entity.
 * @param key    SCRIPT_KEY_* constant.
 * @param view   Entity snapshot view. Must not be NULL.
 * @return true on success, false if handle out of range or view is NULL.
 */
bool aegis_op_get_attr(aegis_register_t *dst,
                       uint32_t handle,
                       uint16_t key,
                       const struct script_entity_view *view);

/**
 * @brief Get number of active entities in snapshot.
 *
 * @param dst  Destination register (u32). Must not be NULL.
 * @param view Entity snapshot view. Must not be NULL.
 * @return true on success, false if view is NULL.
 */
bool aegis_op_entity_count(aegis_register_t *dst,
                           const struct script_entity_view *view);

/**
 * @brief Get entity handle at snapshot index (for iteration).
 *
 * @param dst      Destination register (u32). Must not be NULL.
 * @param idx_reg  Register containing the iteration index (u32).
 * @param view     Entity snapshot view. Must not be NULL.
 * @return true on success, false if index out of range or view is NULL.
 */
bool aegis_op_entity_at(aegis_register_t *dst,
                        const aegis_register_t *idx_reg,
                        const struct script_entity_view *view);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_OPS_ENTITY_H */
