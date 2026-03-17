/**
 * @file edit_entity_json.h
 * @brief Entity JSON serialization and deserialization.
 *
 * Provides arena-based serialization of edit_entity_t to json_value_t
 * objects (for server command responses) and parsing of json_value_t
 * objects back into edit_entity_t snapshots (for client-side sync).
 *
 * Serializes ALL entity fields:
 *   Static: id, name, type, pos, orient, scale, rot, pivot_offset,
 *           body_index, hidden, pending_delete, materials
 *   Dynamic: all entity_attrs_t entries as [key, type, value] triples
 *
 * Thread safety: not thread-safe. Caller must synchronize access.
 */
#ifndef FERRUM_EDITOR_EDIT_ENTITY_JSON_H
#define FERRUM_EDITOR_EDIT_ENTITY_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declarations. */
struct edit_entity;
struct json_value;
struct json_arena;

/**
 * @brief Estimate arena bytes needed for serializing entities.
 *
 * Returns a conservative upper bound for the arena space needed to
 * serialize `entity_count` entities with a total of `total_attr_count`
 * dynamic attributes across all entities.
 *
 * @param entity_count      Number of entities to serialize.
 * @param total_attr_count  Sum of attrs.count across all entities.
 * @return Estimated arena bytes (always > 0).
 */
size_t edit_entity_json_arena_bytes(uint32_t entity_count,
                                    uint32_t total_attr_count);

/**
 * @brief Serialize a single entity into a JSON object value.
 *
 * Allocates all sub-arrays (keys, values, vec3 items, etc.) from the
 * provided arena. The resulting json_value_t is valid for the lifetime
 * of the arena.
 *
 * @param ent    Entity to serialize. Must not be NULL.
 * @param eid    Entity ID (slot index).
 * @param out    Output JSON value (set to JSON_OBJECT on success).
 * @param arena  Arena to allocate from. Must not be NULL.
 * @return true on success, false if arena space is insufficient.
 *
 * Side effects: bumps arena->used.
 */
bool edit_entity_json_build(const struct edit_entity *ent, uint32_t eid,
                            struct json_value *out, struct json_arena *arena);

/**
 * @brief Parse a JSON object into an entity snapshot.
 *
 * Fills the snapshot with data from the JSON object. Missing fields
 * receive sensible defaults (scale=1, body_index=UINT32_MAX, etc.).
 * The snapshot's active flag is always set to true.
 *
 * Dynamic attributes in the "attrs" array are deserialized into the
 * snapshot's entity_attrs_t block via entity_attrs_set().
 *
 * @param item      JSON object to parse (type must be JSON_OBJECT).
 * @param snapshot  Output entity snapshot. Must not be NULL.
 *                  Zeroed and initialized before filling.
 *
 * Side effects: calls entity_attrs_init + entity_attrs_set on snapshot.
 */
void edit_entity_json_parse(const struct json_value *item,
                            struct edit_entity *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_ENTITY_JSON_H */
