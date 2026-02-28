/**
 * @file cmd_list_types.c
 * @brief List available entity types command.
 *
 * Returns an array of type names from the entity type registry.
 * Used by TUI to populate spawn help text and tab completion.
 *
 * JSON response result: ["box","sphere","capsule"]
 *
 * Non-static functions: cmd_list_types (1).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_entity.h"
#include <string.h>

bool cmd_list_types(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena) {
    (void)d;
    (void)args;

    /* Get the type registry. */
    uint32_t count = 0;
    const edit_entity_type_info_t *types = edit_entity_type_registry(&count);

    /* Build a JSON array of type name strings. */
    /* Allocate array items from the response arena. */
    size_t alloc_size = count * sizeof(json_value_t);
    size_t aligned = (alloc_size + 7u) & ~(size_t)7u;
    if (arena->used + aligned > arena->cap) return false;
    json_value_t *items = (json_value_t *)(arena->buf + arena->used);
    arena->used += aligned;

    for (uint32_t i = 0; i < count; i++) {
        items[i].type       = JSON_STRING;
        items[i].string.ptr = types[i].name;
        items[i].string.len = (uint32_t)strlen(types[i].name);
    }

    result->type        = JSON_ARRAY;
    result->array.items = items;
    result->array.count = count;
    return true;
}
