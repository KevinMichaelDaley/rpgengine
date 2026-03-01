/**
 * @file edit_script_rebase.h
 * @brief Rebase script entity updates onto authoritative entity state.
 *
 * Reads a packed update blob (produced by script_env_write_attr) and
 * applies each attribute write to the corresponding entity in the store.
 * Only attributes explicitly written by the script are applied — all
 * other fields retain their physics/native values.
 *
 * Ownership: borrows store and blob; does not allocate memory.
 * Nullability: NULL store or blob returns a zero result.
 * Thread safety: must be called from the main tick thread only.
 */
#ifndef FERRUM_EDITOR_EDIT_SCRIPT_REBASE_H
#define FERRUM_EDITOR_EDIT_SCRIPT_REBASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct edit_entity_store;

/**
 * @brief Result of a rebase operation.
 */
typedef struct script_rebase_result {
    uint32_t applied; /**< Number of entity updates successfully applied. */
    uint32_t skipped; /**< Number of entity updates skipped (missing/deleted). */
} script_rebase_result_t;

/**
 * @brief Apply script entity updates from the blob onto the entity store.
 *
 * Iterates the packed blob of script_entity_update_t headers and their
 * attribute writes. For each update:
 *   - Looks up the entity by entity_id in the store.
 *   - Skips if entity is missing, inactive, or out of range.
 *   - For well-known keys (POS, ROT, SCALE, TYPE, BODY_IDX, NAME):
 *     applies directly to the entity's fixed fields.
 *   - For user/dynamic keys: writes to entity_attrs_t via entity_attrs_set.
 *   - Validates payload size for well-known keys; skips on mismatch.
 *
 * @param store  Entity store to update (NULL-safe, returns zero result).
 * @param blob   Packed update blob (NULL-safe with used_bytes==0).
 * @param used_bytes  Number of valid bytes in the blob.
 * @return Result with counts of applied and skipped updates.
 *
 * Side effects: mutates entities in the store.
 * Error semantics: never crashes; logs nothing (caller may log skipped).
 */
script_rebase_result_t script_rebase_apply(struct edit_entity_store *store,
                                           const uint8_t *blob,
                                           uint32_t used_bytes);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_SCRIPT_REBASE_H */
