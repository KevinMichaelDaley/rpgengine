/**
 * @file cmd_list_entities.c
 * @brief List active entities, with optional regex name filter.
 *
 * Returns a JSON array of entity info objects:
 *   [{"id":0,"name":"player","type":"box"}, ...]
 *
 * Optional arg "pattern" applies a POSIX extended regex to entity names.
 * Unnamed entities are included when no pattern is given, excluded otherwise.
 *
 * Non-static functions: cmd_list_entities (1).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include <regex.h>
#include <string.h>

/**
 * @brief Check if an entity matches the filter criteria.
 * @return true if entity should be included in results.
 */
static bool matches_(const edit_entity_t *ent, bool has_pattern,
                     const regex_t *regex) {
    if (!ent->active) return false;
    if (!has_pattern) return true;
    /* Pattern given but entity unnamed → exclude. */
    if (ent->name[0] == '\0') return false;
    return regexec(regex, ent->name, 0, NULL, 0) == 0;
}

/**
 * @brief Look up type name string from type ID via registry.
 */
static const char *type_name_(uint32_t type_id) {
    uint32_t count = 0;
    const edit_entity_type_info_t *types = edit_entity_type_registry(&count);
    for (uint32_t i = 0; i < count; i++) {
        if (types[i].type_id == type_id) return types[i].name;
    }
    return "unknown";
}

bool cmd_list_entities(edit_dispatch_t *d, const json_value_t *args,
                       json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities) return false;

    const edit_entity_store_t *store = ctx->entities;

    /* Extract optional pattern. */
    const json_value_t *pat_val = json_object_get(args, "pattern");
    regex_t regex;
    bool has_pattern = false;

    if (pat_val && pat_val->type == JSON_STRING && pat_val->string.len > 0) {
        char pat_buf[256];
        if (!json_string_copy(pat_val, pat_buf, sizeof(pat_buf))) return false;
        int rc = regcomp(&regex, pat_buf, REG_EXTENDED | REG_NOSUB);
        if (rc != 0) return false;
        has_pattern = true;
    }

    /* Count matching entities. */
    uint32_t match_count = 0;
    for (uint32_t i = 0; i < store->capacity; i++) {
        if (matches_(&store->entities[i], has_pattern, &regex)) {
            match_count++;
        }
    }

    /* Allocate from arena: items + keys + key_lens + vals (3 per object). */
    size_t items_sz = ((match_count * sizeof(json_value_t) + 7) & ~(size_t)7);
    size_t keys_sz = ((match_count * 3 * sizeof(const char *) + 7) & ~(size_t)7);
    size_t klens_sz = ((match_count * 3 * sizeof(uint32_t) + 7) & ~(size_t)7);
    size_t vals_sz = ((match_count * 3 * sizeof(json_value_t) + 7) & ~(size_t)7);
    size_t total = items_sz + keys_sz + klens_sz + vals_sz;

    if (arena->used + total > arena->cap) {
        if (has_pattern) regfree(&regex);
        return false;
    }

    json_value_t *items = (json_value_t *)(arena->buf + arena->used);
    arena->used += items_sz;
    const char **all_keys = (const char **)(arena->buf + arena->used);
    arena->used += keys_sz;
    uint32_t *all_klens = (uint32_t *)(arena->buf + arena->used);
    arena->used += klens_sz;
    json_value_t *all_vals = (json_value_t *)(arena->buf + arena->used);
    arena->used += vals_sz;

    /* Static key names. */
    static const char *key_strs[] = {"id", "name", "type"};
    static const uint32_t key_lens[] = {2, 4, 4};

    /* Build result objects. */
    uint32_t idx = 0;
    for (uint32_t i = 0; i < store->capacity && idx < match_count; i++) {
        if (!matches_(&store->entities[i], has_pattern, &regex)) continue;

        const edit_entity_t *ent = &store->entities[i];
        const char **keys = &all_keys[idx * 3];
        uint32_t *klens = &all_klens[idx * 3];
        json_value_t *vals = &all_vals[idx * 3];

        for (int k = 0; k < 3; k++) {
            keys[k] = key_strs[k];
            klens[k] = key_lens[k];
        }

        vals[0].type = JSON_NUMBER;
        vals[0].number = (double)i;

        vals[1].type = JSON_STRING;
        vals[1].string.ptr = ent->name[0] ? ent->name : "";
        vals[1].string.len = (uint32_t)strlen(vals[1].string.ptr);

        const char *tname = type_name_(ent->type);
        vals[2].type = JSON_STRING;
        vals[2].string.ptr = tname;
        vals[2].string.len = (uint32_t)strlen(tname);

        items[idx].type = JSON_OBJECT;
        items[idx].object.keys = keys;
        items[idx].object.key_lens = klens;
        items[idx].object.vals = vals;
        items[idx].object.count = 3;
        idx++;
    }

    if (has_pattern) regfree(&regex);

    result->type = JSON_ARRAY;
    result->array.items = items;
    result->array.count = match_count;
    return true;
}
