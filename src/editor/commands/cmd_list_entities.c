/**
 * @file cmd_list_entities.c
 * @brief List active entities with pagination.
 *
 * Returns a paginated JSON result:
 *   {"entities":[{...},...], "total":N, "offset":O}
 *
 * Optional args:
 *   "pattern" — POSIX extended regex filter on entity names
 *   "offset"  — skip first N matching entities (default 0)
 *   "limit"   — max entities per page (default 200)
 *
 * The client iterates pages by advancing offset until
 * offset + returned_count >= total.
 *
 * Non-static functions: cmd_list_entities (1).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include <regex.h>
#include <string.h>

/** Default and maximum entities per page. */
#define LIST_DEFAULT_LIMIT 200
#define LIST_MAX_LIMIT     500

/** Fields per entity object: id, name, type, pos, rot, scale. */
#define FIELDS_PER_ENTITY 6

/** Number of vec3 arrays per entity (pos, rot, scale). */
#define VEC3_ARRAYS 3

/**
 * @brief Check if an entity matches the filter criteria.
 * @return true if entity should be included in results.
 */
static bool matches_(const edit_entity_t *ent, bool has_pattern,
                     const regex_t *regex) {
    if (!ent->active) return false;
    if (!has_pattern) return true;
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

    /* Extract pagination args. */
    uint32_t req_offset = 0;
    uint32_t req_limit = LIST_DEFAULT_LIMIT;
    if (args) {
        const json_value_t *off_val = json_object_get(args, "offset");
        if (off_val && off_val->type == JSON_NUMBER && off_val->number >= 0) {
            req_offset = (uint32_t)off_val->number;
        }
        const json_value_t *lim_val = json_object_get(args, "limit");
        if (lim_val && lim_val->type == JSON_NUMBER && lim_val->number > 0) {
            req_limit = (uint32_t)lim_val->number;
            if (req_limit > LIST_MAX_LIMIT) req_limit = LIST_MAX_LIMIT;
        }
    }

    /* Count total matching entities. */
    uint32_t total_count = 0;
    for (uint32_t i = 0; i < store->capacity; i++) {
        if (matches_(&store->entities[i], has_pattern, &regex)) {
            total_count++;
        }
    }

    /* Determine page size (how many entities to include in this response). */
    uint32_t page_count = 0;
    if (req_offset < total_count) {
        page_count = total_count - req_offset;
        if (page_count > req_limit) page_count = req_limit;
    }

    /* Allocate from arena for the page of entities.
     * Result structure: {"entities":[...], "total":N, "offset":N}
     * = 1 wrapper object with 3 keys, page_count entity objects,
     *   each with 6 fields + 3 vec3 arrays. */
    size_t items_sz = ((page_count * sizeof(json_value_t) + 7) & ~(size_t)7);
    size_t keys_sz = ((page_count * FIELDS_PER_ENTITY * sizeof(const char *) + 7) & ~(size_t)7);
    size_t klens_sz = ((page_count * FIELDS_PER_ENTITY * sizeof(uint32_t) + 7) & ~(size_t)7);
    size_t vals_sz = ((page_count * FIELDS_PER_ENTITY * sizeof(json_value_t) + 7) & ~(size_t)7);
    size_t vec3_sz = ((page_count * VEC3_ARRAYS * 3 * sizeof(json_value_t) + 7) & ~(size_t)7);
    /* Wrapper object: 3 keys, 3 key_lens, 3 vals. */
    size_t wrap_keys_sz = ((3 * sizeof(const char *) + 7) & ~(size_t)7);
    size_t wrap_klens_sz = ((3 * sizeof(uint32_t) + 7) & ~(size_t)7);
    size_t wrap_vals_sz = ((3 * sizeof(json_value_t) + 7) & ~(size_t)7);
    size_t total = items_sz + keys_sz + klens_sz + vals_sz + vec3_sz
                 + wrap_keys_sz + wrap_klens_sz + wrap_vals_sz;

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
    json_value_t *vec3_items = (json_value_t *)(arena->buf + arena->used);
    arena->used += vec3_sz;
    const char **wrap_keys = (const char **)(arena->buf + arena->used);
    arena->used += wrap_keys_sz;
    uint32_t *wrap_klens = (uint32_t *)(arena->buf + arena->used);
    arena->used += wrap_klens_sz;
    json_value_t *wrap_vals = (json_value_t *)(arena->buf + arena->used);
    arena->used += wrap_vals_sz;

    /* Static key names for entity fields. */
    static const char *ent_key_strs[] = {"id", "name", "type", "pos", "rot", "scale"};
    static const uint32_t ent_key_lens[] = {2, 4, 4, 3, 3, 5};

    /* Build entity objects for this page. */
    uint32_t idx = 0;       /* index into page items */
    uint32_t match_idx = 0; /* index among all matching entities */
    for (uint32_t i = 0; i < store->capacity && idx < page_count; i++) {
        if (!matches_(&store->entities[i], has_pattern, &regex)) continue;

        /* Skip entities before the requested offset. */
        if (match_idx < req_offset) {
            match_idx++;
            continue;
        }
        match_idx++;

        const edit_entity_t *ent = &store->entities[i];
        const char **keys = &all_keys[idx * FIELDS_PER_ENTITY];
        uint32_t *klens = &all_klens[idx * FIELDS_PER_ENTITY];
        json_value_t *vals = &all_vals[idx * FIELDS_PER_ENTITY];

        for (int k = 0; k < FIELDS_PER_ENTITY; k++) {
            keys[k] = ent_key_strs[k];
            klens[k] = ent_key_lens[k];
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

        /* pos, rot, scale vec3 arrays. */
        json_value_t *v3 = &vec3_items[idx * VEC3_ARRAYS * 3];
        const float *vecs[3] = {ent->pos, ent->rot, ent->scale};
        for (int vi = 0; vi < 3; vi++) {
            json_value_t *elems = &v3[vi * 3];
            for (int c = 0; c < 3; c++) {
                elems[c].type = JSON_NUMBER;
                elems[c].number = (double)vecs[vi][c];
            }
            vals[3 + vi].type = JSON_ARRAY;
            vals[3 + vi].array.items = elems;
            vals[3 + vi].array.count = 3;
        }

        items[idx].type = JSON_OBJECT;
        items[idx].object.keys = keys;
        items[idx].object.key_lens = klens;
        items[idx].object.vals = vals;
        items[idx].object.count = FIELDS_PER_ENTITY;
        idx++;
    }

    if (has_pattern) regfree(&regex);

    /* Build wrapper object: {"entities":[...], "total":N, "offset":N} */
    wrap_keys[0] = "entities";  wrap_klens[0] = 8;
    wrap_keys[1] = "total";     wrap_klens[1] = 5;
    wrap_keys[2] = "offset";    wrap_klens[2] = 6;

    wrap_vals[0].type = JSON_ARRAY;
    wrap_vals[0].array.items = items;
    wrap_vals[0].array.count = idx;

    wrap_vals[1].type = JSON_NUMBER;
    wrap_vals[1].number = (double)total_count;

    wrap_vals[2].type = JSON_NUMBER;
    wrap_vals[2].number = (double)req_offset;

    result->type = JSON_OBJECT;
    result->object.keys = wrap_keys;
    result->object.key_lens = wrap_klens;
    result->object.vals = wrap_vals;
    result->object.count = 3;
    return true;
}
