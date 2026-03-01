/**
 * @file aegis_ops_update.h
 * @brief Aegis update set construction instruction handlers.
 *
 * Per ref/aegis_bytecode_spec.md §3.3 (Update construction group).
 *
 * Scripts build updates via: build_update → target_entity → set_field →
 * add_hint → push_update. The update set accumulates in the VM and is
 * returned to the engine on yield.
 *
 * Ownership: VM owns the update set. Caller owns register pointers.
 * Nullability: update_set must not be NULL.
 */

#ifndef FERRUM_AEGIS_OPS_UPDATE_H
#define FERRUM_AEGIS_OPS_UPDATE_H

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/aegis/aegis_types.h"
#include "ferrum/aegis/aegis_update.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create an empty update builder.
 *
 * Initializes a staging slot for the next update. The register holds
 * the builder index (always 0 — single builder per VM).
 *
 * @param dst        Destination register (receives builder handle).
 * @param staging    Staging update to clear.
 * @return true always.
 */
bool aegis_op_build_update(aegis_register_t *dst,
                           aegis_state_update_t *staging);

/**
 * @brief Set target entity ID for the current update.
 *
 * @param staging    Staging update to modify.
 * @param entity_reg Register containing entity_id (u32).
 * @return true always.
 */
bool aegis_op_target_entity(aegis_state_update_t *staging,
                            const aegis_register_t *entity_reg);

/**
 * @brief Add an attribute write to the staging update.
 *
 * Copies value from register into the staging update's value buffer.
 * Type and size are inferred from the key for well-known keys,
 * or must be explicitly provided for user keys.
 *
 * @param staging  Staging update to modify.
 * @param key      SCRIPT_KEY_* constant (immediate).
 * @param val_reg  Register containing the value to write.
 * @return true on success, false if key type cannot be inferred.
 */
bool aegis_op_set_field(aegis_state_update_t *staging,
                        uint16_t key,
                        const aegis_register_t *val_reg);

/**
 * @brief Add a validation hint flag to the staging update.
 *
 * @param staging   Staging update to modify.
 * @param hint_type AEGIS_HINT_* flag value (immediate).
 * @return true always.
 */
bool aegis_op_add_hint(aegis_state_update_t *staging,
                       uint32_t hint_type);

/**
 * @brief Finalize and append the staging update to the update set.
 *
 * Copies the staging update into the next slot in the update set.
 * Enforces the max_updates capacity limit.
 *
 * @param set     Update set to append to.
 * @param staging Staging update to push (copied, then cleared).
 * @return true on success, false if at capacity.
 */
bool aegis_op_push_update(aegis_update_set_t *set,
                          aegis_state_update_t *staging);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_OPS_UPDATE_H */
