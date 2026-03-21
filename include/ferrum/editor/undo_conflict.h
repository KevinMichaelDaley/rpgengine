/**
 * @file undo_conflict.h
 * @brief Undo conflict detection for rebaseable redo.
 *
 * Extracts conflict keys from undo entries and checks whether two
 * operations conflict. Currently all operation types use entity_id
 * as the conflict key; future types (vertex, texel, keyframe) will
 * use compound keys.
 *
 * Thread safety: all functions are pure (no side effects).
 */
#ifndef FERRUM_EDITOR_UNDO_CONFLICT_H
#define FERRUM_EDITOR_UNDO_CONFLICT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration. */
struct edit_undo_entry;

/**
 * @brief Conflict key extracted from an undo entry.
 *
 * For entity-level operations (move, rotate, scale, spawn, delete),
 * the key is simply the entity_id. Future operation types may use
 * additional fields (mesh_id, vertex range, UV region).
 */
typedef struct undo_conflict_key {
    uint32_t entity_id;     /**< Primary conflict dimension. */
    uint32_t sub_index;     /**< Sub-resource (bone index, 0 for entity-level). */
    uint32_t key_type;      /**< 0 = entity-level, 1 = bone-level. */
} undo_conflict_key_t;

/**
 * @brief Extract a conflict key from an undo entry.
 *
 * @param entry  Undo entry (NULL returns zero key).
 * @return Conflict key.
 */
undo_conflict_key_t undo_conflict_key_extract(const struct edit_undo_entry *entry);

/**
 * @brief Check whether two conflict keys overlap.
 *
 * Two keys conflict if they affect the same resource. For entity-level
 * operations, this means the same entity_id.
 *
 * @param a  First key.
 * @param b  Second key.
 * @return true if the keys conflict.
 */
bool undo_conflict_check(const undo_conflict_key_t *a,
                          const undo_conflict_key_t *b);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_UNDO_CONFLICT_H */
